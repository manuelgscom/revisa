#include "core/check/check.hpp"

#include <utility>

#include "core/classify/classify.hpp"
#include "core/diff/diff.hpp"
#include "core/guard/guard.hpp"
#include "core/infer/prompt.hpp"
#include "core/protect/protect.hpp"
#include "core/segment/segment.hpp"
#include "core/text/text.hpp"

namespace revisa::check {

namespace {

// A segment that goes to the model: its NFC text and its protected spans.
struct Prepared {
    text::Normalized nfc;
    std::vector<protect::Span> spans;
};

// nullopt if the segment is skipped before the model (too long, special token, fully protected);
// why (if given) gets the reason, Skip::None when it goes to the model.
std::optional<Prepared> prepare(std::string_view text, const segment::Segment& seg, Skip* why = nullptr) {
    const auto skip = [why](Skip s) -> std::optional<Prepared> {
        if (why) *why = s;
        return std::nullopt;
    };
    if (why) *why = Skip::None;
    if (seg.too_long) return skip(Skip::Long);
    Prepared p{text::nfc(text.substr(seg.start, seg.end - seg.start)), {}};
    if (protect::has_special_token(p.nfc.text)) return skip(Skip::Special);
    p.spans = protect::protect(p.nfc.text);
    if (protect::fully_protected(p.nfc.text, p.spans)) return skip(Skip::Protected);
    return p;
}

// diff + classify, with the context taken from original.
std::vector<guard::Item> classified(std::string_view original, std::string_view corrected) {
    std::vector<guard::Item> items;
    for (diff::Edit& e : diff::diff(original, corrected)) {
        const classify::Context ctx{original.substr(0, e.start), original.substr(e.end)};
        classify::Result r = classify::classify(e.from, e.to, ctx);
        items.push_back({std::move(e), std::move(r)});
    }
    return items;
}

std::string to_nfc_trimmed(std::string_view model_output) {
    return text::to_nfc(infer::trim_like_python(model_output));
}

// Fills the UTF-16 offsets and builds text with the edits applied.
std::string finish(std::string_view text, std::vector<Edit>& edits) {
    const text::Utf16Index idx(text);
    for (Edit& e : edits) {
        e.start = idx.at(e.byte_start);
        e.end = idx.at(e.byte_end);
    }
    return check::apply(text, edits);
}

}  // namespace

std::string apply(std::string_view text, std::span<const Edit> edits) {
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    for (const Edit& e : edits) {
        // Out of order, overlapping or out of range: ignored, never undefined behaviour.
        if (e.byte_start < pos || e.byte_start > e.byte_end || e.byte_end > text.size()) continue;
        out.append(text.substr(pos, e.byte_start - pos));
        out += e.to;
        pos = e.byte_end;
    }
    out.append(text.substr(pos));
    return out;
}

bool needs_model(std::string_view text) {
    for (const segment::Segment& seg : segment::segment(text))
        if (prepare(text, seg)) return true;
    return false;
}

std::vector<ModelSegment> model_segments(std::string_view text) {
    std::vector<ModelSegment> out;
    for (const segment::Segment& seg : segment::segment(text)) {
        ModelSegment m;
        m.start = seg.start;
        m.end = seg.end;
        const std::optional<Prepared> p = prepare(text, seg, &m.skip);
        m.text = p ? p->nfc.text : text::to_nfc(text.substr(seg.start, seg.end - seg.start));
        out.push_back(std::move(m));
    }
    return out;
}

const char* skip_name(Skip s) {
    switch (s) {
        case Skip::None: return "";
        case Skip::Long: return "long";
        case Skip::Protected: return "protected";
        case Skip::Special: return "special";
    }
    return "";
}

Result check_text(std::string_view text, infer::Engine& engine, const Options& options) {
    Result r;
    for (const segment::Segment& seg : segment::segment(text)) {
        const std::optional<Prepared> p = prepare(text, seg);
        if (!p) {
            ++r.segments_skipped;
            continue;
        }
        const std::string& s = p->nfc.text;
        const infer::EngineResult out = engine.correct(s);
        const std::string o = out.text ? to_nfc_trimmed(*out.text) : std::string();
        r.timings.push_back({out.timings.total_ms - out.timings.load_ms, out.text && o != s,
                             out.timings.segment_tokens});
        if (!out.text) {
            if (out.error == "cap_reached" || out.error == "too_long") {
                ++r.segments_skipped;
                continue;
            }
            r.error = out.error.empty() ? "decode_failed" : out.error;
            r.error_detail = out.error_detail;
            r.edits.clear();
            return r;
        }
        if (options.guards && guard::invalid_output(s, o)) {
            ++r.segments_skipped;
            continue;
        }
        if (o == s) continue;
        guard::Outcome g;
        if (options.guards) {
            g = guard::apply(s, o, classified(s, o), p->spans, options.disabled, options.thresholds);
        } else {
            g.kept = classified(s, o);
        }
        if (g.rejection != guard::Rejection::None) {
            ++r.segments_skipped;
            continue;
        }
        for (const guard::Item& it : g.kept) {
            const auto [a, b] = p->nfc.map(it.edit.start, it.edit.end);
            Edit e;
            e.byte_start = seg.start + a;
            e.byte_end = seg.start + b;
            e.from = std::string(text.substr(e.byte_start, e.byte_end - e.byte_start));
            e.to = it.edit.to;
            if (e.from == e.to) continue;
            e.type = it.result.type;
            e.explanation = it.result.explanation;
            r.edits.push_back(std::move(e));
        }
    }
    r.corrected = finish(text, r.edits);
    r.changed = r.corrected != text;
    return r;
}

std::vector<Edit> diff_pair(std::string_view original, std::string_view corrected) {
    std::vector<Edit> edits;
    for (guard::Item& it : classified(original, corrected)) {
        Edit e;
        e.byte_start = it.edit.start;
        e.byte_end = it.edit.end;
        e.from = std::move(it.edit.from);
        e.to = std::move(it.edit.to);
        e.type = it.result.type;
        e.explanation = std::move(it.result.explanation);
        edits.push_back(std::move(e));
    }
    finish(original, edits);
    return edits;
}

}  // namespace revisa::check
