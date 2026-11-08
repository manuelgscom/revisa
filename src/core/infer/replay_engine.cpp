#include "core/infer/replay_engine.hpp"

#include <algorithm>
#include <utility>

#include "core/infer/prompt.hpp"
#include "core/segment/segment.hpp"
#include "core/text/text.hpp"

namespace revisa::infer {

namespace {

bool only_space(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const text::Decoded d = text::decode(s, i);
        if (!text::is_space(d.cp)) return false;
        i += d.len;
    }
    return true;
}

// corrected with the leading and trailing whitespace of text.
std::string same_edges(std::string_view text, std::string_view corrected) {
    const std::string_view core = trim_like_python(text);
    if (core.empty()) return std::string(text);
    const auto lead = static_cast<std::size_t>(core.data() - text.data());
    std::string out(text.substr(0, lead));
    out += trim_like_python(corrected);
    out += text.substr(lead + core.size());
    return out;
}

}  // namespace

ReplayEngine::ReplayEngine(std::string_view text, std::string_view corrected) {
    const text::Normalized nt = text::nfc(text);
    const std::vector<diff::Edit> edits = diff::diff(nt.text, text::to_nfc(same_edges(text, corrected)));

    // Each segment and its byte range [start, end) in nt.text (orig_start is non-decreasing).
    std::vector<std::size_t> ends;
    for (const segment::Segment& seg : segment::segment(text)) {
        Seg s;
        s.nfc = text::to_nfc(text.substr(seg.start, seg.end - seg.start));
        const auto at = [&nt](std::size_t orig) {
            return static_cast<std::size_t>(std::lower_bound(nt.orig_start.begin(), nt.orig_start.end(), orig) -
                                            nt.orig_start.begin());
        };
        s.start = at(seg.start);
        const std::size_t end = at(seg.end);
        s.matched = std::string_view(nt.text).substr(s.start, end - s.start) == s.nfc;
        segs_.push_back(std::move(s));
        ends.push_back(end);
    }

    for (const diff::Edit& e : edits) {
        std::size_t owner = segs_.size();
        int owners = 0;
        for (std::size_t i = 0; i < segs_.size(); ++i) {
            if (segs_[i].start <= e.start && e.end <= ends[i]) {
                owner = i;
                ++owners;
            }
        }
        if (owners != 1) {
            // Whitespace between segments only: the path never touches it, nothing is lost.
            if (owners == 0 && only_space(e.from) && only_space(e.to)) continue;
            ++dropped_;
            continue;
        }
        diff::Edit rel = e;
        rel.start -= segs_[owner].start;
        rel.end -= segs_[owner].start;
        segs_[owner].edits.push_back(std::move(rel));
    }
}

EngineResult ReplayEngine::correct(std::string_view segment) {
    EngineResult r;
    for (std::size_t i = next_; i < segs_.size(); ++i) {
        const Seg& s = segs_[i];
        if (s.nfc != segment) continue;
        next_ = i + 1;
        if (!s.matched) {
            dropped_ += static_cast<int>(s.edits.size());
            r.text = std::string(segment);
            return r;
        }
        std::string out;
        std::size_t pos = 0;
        for (const diff::Edit& e : s.edits) {
            out.append(s.nfc, pos, e.start - pos);
            out += e.to;
            pos = e.end;
        }
        out.append(s.nfc, pos);
        r.text = std::move(out);
        return r;
    }
    ++dropped_;  // never asked by check_text over the same text
    r.text = std::string(segment);
    return r;
}

}  // namespace revisa::infer
