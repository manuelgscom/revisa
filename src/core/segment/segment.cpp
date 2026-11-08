// Segmenter. See segment.hpp for the contract.
//
// Rules decided here (tests/segment/*.jsonl):
//   - A sentence end is a run of '.', '?', '!', '…' ("..." is a run of dots), then any closing
//     quotes or brackets (» ” ’ " ' ) ]), then whitespace, then any opening quotes, brackets or
//     dashes (« “ ‘ " ' ( [ — – -), then an uppercase letter, '¿' or '¡'. The cut falls right after
//     the closers, so they stay with the sentence they close.
//   - Every line break cuts: \n, \r\n, \r, U+0085, U+2028 and U+2029.
//   - A run that is exactly one '.' does not cut when:
//       * any dot of an abbreviation of catalog/abreviaturas.txt sits on it (the internal one of
//         "EE. UU." too), except the final dot of an abbreviation marked "fin" (etc.): that one
//         doubles as the sentence's period, so it cuts like a normal '.';
//       * the word before it is a single uppercase letter (initials: J. R. R. Tolkien);
//       * the word before it is a number that starts the line (a list item: "1. Introducción").
//   - Numbers (3.5, 1.000) and URLs never cut because their dots are not followed by whitespace;
//     besides, no terminator inside a protected span (protect.hpp: URLs, `code`, paths…) cuts.
//   - Segments longer than kMaxSegmentChars are split after every ';' or ':' that is followed by
//     whitespace, and consecutive parts are merged back greedily while they fit.

#include "core/segment/segment.hpp"

#include <algorithm>
#include <string>

#include "core/catalog/catalog.hpp"
#include "core/infer/prompt.hpp"
#include "core/protect/protect.hpp"
#include "core/text/text.hpp"

namespace revisa::segment {

namespace {

struct Cp {
    std::size_t pos = 0, len = 0;
    char32_t cp = 0;
};

struct Abbrev {
    std::string text;
    bool can_end = false;
};

const std::vector<Abbrev>& abbreviations() {
    static const std::vector<Abbrev> list = [] {
        std::vector<Abbrev> out;
        for (const catalog::Row& r : catalog::rows("abreviaturas.txt")) {
            if (r.cols.empty() || r.cols[0].empty()) continue;
            const bool can_end = r.cols.size() > 1 && r.cols[1] == "fin";
            const std::string a(r.cols[0]);
            out.push_back({a, can_end});
            // The same abbreviation with its first letter in uppercase (start of a sentence).
            const text::Decoded d = text::decode(a, 0);
            const char32_t up = text::to_upper(d.cp);
            if (up != d.cp) {
                std::string cap;
                text::append_utf8(cap, up);
                cap.append(a, d.len, std::string::npos);
                out.push_back({cap, can_end});
            }
        }
        return out;
    }();
    return list;
}

bool is_terminator(char32_t c) { return c == U'.' || c == U'?' || c == U'!' || c == U'…'; }

bool is_closer(char32_t c) {
    return c == U'»' || c == U'”' || c == U'’' || c == U'"' || c == U'\'' || c == U')' || c == U']';
}

bool is_opener(char32_t c) {
    return c == U'«' || c == U'“' || c == U'‘' || c == U'"' || c == U'\'' || c == U'(' || c == U'[' ||
           c == U'—' || c == U'–' || c == U'-';
}

bool is_line_break(char32_t c) {
    return c == U'\n' || c == U'\r' || c == 0x85 || c == 0x2028 || c == 0x2029;
}

bool is_word_char(char32_t c) { return text::is_letter(c) || text::is_digit(c) || text::is_mark(c); }

class Segmenter {
public:
    explicit Segmenter(std::string_view text) : text_(text) {
        for (std::size_t i = 0; i < text.size();) {
            const text::Decoded d = text::decode(text, i);
            cps_.push_back({i, d.len, d.cp});
            i += d.len;
        }
    }

    std::vector<Segment> run() {
        std::size_t ls = 0;
        while (ls < cps_.size()) {
            std::size_t le = ls;
            while (le < cps_.size() && !is_line_break(cps_[le].cp)) ++le;
            line(ls, le);
            if (le < cps_.size() && cps_[le].cp == U'\r' && le + 1 < cps_.size() && cps_[le + 1].cp == U'\n') ++le;
            ls = le + 1;
        }
        return std::move(out_);
    }

private:
    std::size_t byte(std::size_t i) const { return i < cps_.size() ? cps_[i].pos : text_.size(); }

    // Sentences of the line [ls, le) (code point indices).
    void line(std::size_t ls, std::size_t le) {
        const std::size_t lb = byte(ls), lend = byte(le);
        const std::string_view ltext = text_.substr(lb, lend - lb);
        spans_ = protect::protect(ltext);
        line_start_ = ls;
        std::size_t piece = lb;
        for (std::size_t i = ls; i < le; ++i) {
            if (!is_terminator(cps_[i].cp)) continue;
            std::size_t j = i;
            while (j < le && is_terminator(cps_[j].cp)) ++j;
            std::size_t k = j;
            while (k < le && is_closer(cps_[k].cp)) ++k;
            const bool cut = k < le && text::is_space(cps_[k].cp) && starts_sentence(k, le) &&
                             !protected_at(cps_[i].pos - lb) && !(j == i + 1 && cps_[i].cp == U'.' && keeps_dot(i));
            if (cut) {
                emit(piece, byte(k));
                piece = byte(k);
            }
            i = std::max(i, j - 1);
        }
        emit(piece, lend);
    }

    // After whitespace at k: openers, then an uppercase letter, '¿' or '¡'.
    bool starts_sentence(std::size_t k, std::size_t le) const {
        while (k < le && text::is_space(cps_[k].cp)) ++k;
        while (k < le && is_opener(cps_[k].cp)) ++k;
        if (k >= le) return false;
        const char32_t c = cps_[k].cp;
        return c == U'¿' || c == U'¡' || (text::is_letter(c) && text::is_upper(c));
    }

    bool protected_at(std::size_t off) const {
        return std::any_of(spans_.begin(), spans_.end(),
                           [&](const protect::Span& s) { return s.start <= off && off < s.end; });
    }

    // true if the single '.' at cps_[i] is not a sentence end (abbreviation, initial, list number).
    bool keeps_dot(std::size_t i) const {
        const std::size_t dot = cps_[i].pos;
        const std::size_t line_byte = byte(line_start_);
        for (const Abbrev& a : abbreviations()) {
            for (std::size_t k = a.text.find('.'); k != std::string::npos; k = a.text.find('.', k + 1)) {
                if (k > dot || dot - k < line_byte) continue;
                const std::size_t start = dot - k;
                if (text_.compare(start, a.text.size(), a.text) != 0) continue;
                if (start > line_byte && is_word_char(prev_cp(start))) continue;
                if (a.can_end && k + 1 == a.text.size()) continue;
                return true;
            }
        }
        // The word right before the dot.
        std::size_t w = i;
        while (w > line_start_ && text::is_letter(cps_[w - 1].cp)) --w;
        const bool boundary = w == line_start_ || !is_word_char(cps_[w - 1].cp);
        if (w + 1 == i && boundary && text::is_upper(cps_[w].cp)) return true;
        std::size_t d = i;
        while (d > line_start_ && text::is_digit(cps_[d - 1].cp)) --d;
        if (d < i) {
            std::size_t b = d;
            while (b > line_start_ && text::is_space(cps_[b - 1].cp)) --b;
            if (b == line_start_) return true;
        }
        return false;
    }

    char32_t prev_cp(std::size_t byte_pos) const {
        const auto it = std::lower_bound(cps_.begin(), cps_.end(), byte_pos,
                                         [](const Cp& c, std::size_t b) { return c.pos < b; });
        return it == cps_.begin() ? U' ' : std::prev(it)->cp;
    }

    // Trims [a, b) and appends it (split if too long).
    void emit(std::size_t a, std::size_t b) {
        const std::string_view piece = text_.substr(a, b - a);
        const std::string_view t = infer::trim_like_python(piece);
        if (t.empty()) return;
        const std::size_t s = a + static_cast<std::size_t>(t.data() - piece.data());
        const std::size_t e = s + t.size();
        if (text::code_points(t) <= infer::kMaxSegmentChars) {
            out_.push_back({s, e, false});
            return;
        }
        split_long(s, e);
    }

    // Cuts [s, e) after ';' or ':' followed by whitespace, then merges the parts back while they fit.
    void split_long(std::size_t s, std::size_t e) {
        std::vector<std::pair<std::size_t, std::size_t>> parts;
        std::size_t from = s;
        for (std::size_t i = s; i < e;) {
            const text::Decoded d = text::decode(text_, i);
            const std::size_t next = i + d.len;
            if ((d.cp == U';' || d.cp == U':') && next < e && text::is_space(text::decode(text_, next).cp)) {
                parts.emplace_back(from, next);
                from = next;
            }
            i = next;
        }
        parts.emplace_back(from, e);
        std::vector<std::pair<std::size_t, std::size_t>> trimmed;
        for (const auto& [a, b] : parts) {
            const std::string_view p = text_.substr(a, b - a);
            const std::string_view t = infer::trim_like_python(p);
            if (t.empty()) continue;
            const std::size_t ts = a + static_cast<std::size_t>(t.data() - p.data());
            trimmed.emplace_back(ts, ts + t.size());
        }
        auto fits = [&](std::size_t a, std::size_t b) {
            return text::code_points(text_.substr(a, b - a)) <= infer::kMaxSegmentChars;
        };
        std::size_t cs = trimmed.front().first, ce = trimmed.front().second;
        for (std::size_t n = 1; n < trimmed.size(); ++n) {
            if (fits(cs, trimmed[n].second)) {
                ce = trimmed[n].second;
                continue;
            }
            out_.push_back({cs, ce, !fits(cs, ce)});
            cs = trimmed[n].first;
            ce = trimmed[n].second;
        }
        out_.push_back({cs, ce, !fits(cs, ce)});
    }

    std::string_view text_;
    std::vector<Cp> cps_;
    std::vector<Segment> out_;
    std::vector<protect::Span> spans_;  // of the current line, relative to its first byte
    std::size_t line_start_ = 0;
};

}  // namespace

std::vector<Segment> segment(std::string_view text) { return Segmenter(text).run(); }

}  // namespace revisa::segment
