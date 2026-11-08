#include "core/guard/guard.hpp"

#include <algorithm>

#include <limits>
#include <string>
#include <utility>

#include "core/guard/config.hpp"
#include "core/infer/prompt.hpp"
#include "core/text/text.hpp"

namespace revisa::guard {

namespace {

// Code points of s, in order.
std::u32string decode_all(std::string_view s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const text::Decoded d = text::decode(s, i);
        out += d.cp;
        i += d.len;
    }
    return out;
}

// --- Guard 1 ------------------------------------------------------------------------------------

struct QuotePair {
    std::u32string_view open, close;
};
constexpr QuotePair kWrapQuotes[] = {
    {U"«", U"»"}, {U"\"", U"\""}, {U"“", U"”"}, {U"'", U"'"}, {U"‘", U"’"},
};

// The quote pair that wraps the whole of s (at least one code point inside), if any.
const QuotePair* wrapping_quotes(std::string_view s) {
    const std::u32string cps = decode_all(s);
    if (cps.size() < 3) return nullptr;
    for (const QuotePair& q : kWrapQuotes)
        if (cps.front() == q.open[0] && cps.back() == q.close[0]) return &q;
    return nullptr;
}

// --- Guard 4 ------------------------------------------------------------------------------------

bool is_quote(char32_t c) {
    return c == U'"' || c == U'\'' || c == U'«' || c == U'»' || c == U'“' || c == U'”' || c == U'‘' ||
           c == U'’' || c == U'„' || c == U'‹' || c == U'›';
}
bool is_dash(char32_t c) { return c == U'-' || c == U'‐' || c == U'‑' || c == U'–' || c == U'—'; }

// Quotes and dashes mapped to one canonical form each: equal canonical forms = only the style
// of quotes or dashes changed.
std::u32string canonical_style(std::string_view s) {
    std::u32string out = decode_all(s);
    for (char32_t& c : out) {
        if (is_quote(c)) c = U'"';
        else if (is_dash(c)) c = U'-';
    }
    return out;
}

bool only_quotes(std::string_view s) {
    if (s.empty()) return false;
    for (const char32_t c : decode_all(s))
        if (!is_quote(c)) return false;
    return true;
}

bool is_number_space(char32_t c) { return c == U' ' || c == 0x00A0 || c == 0x202F || c == 0x2009; }

// Digits and the signs that format numbers and times (1.000, 3,5, 18:30, 1 000).
bool is_number_char(char32_t c) {
    return text::is_digit(c) || c == U'.' || c == U',' || c == U':' || c == U'\'' || c == U'’' ||
           is_number_space(c);
}

bool has_digit(std::string_view s) {
    for (const char c : s)
        if (c >= '0' && c <= '9') return true;
    return false;
}

bool all_of_cps(std::string_view s, bool (*pred)(char32_t)) {
    for (const char32_t c : decode_all(s))
        if (!pred(c)) return false;
    return true;
}

// The code point just before byte i of s (0 if none) and the one starting at byte i (0 if none).
char32_t cp_before(std::string_view s, std::size_t i) {
    if (i == 0 || i > s.size()) return 0;
    std::size_t j = i - 1;
    while (j > 0 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) --j;
    return text::decode(s, j).cp;
}
char32_t cp_at(std::string_view s, std::size_t i) { return i < s.size() ? text::decode(s, i).cp : 0; }

// An edit that only touches digits or the formatting of a number or a time.
bool numeric_edit(const diff::Edit& e, std::string_view segment) {
    const auto num = [](char32_t c) { return is_number_char(c); };
    if ((has_digit(e.from) || has_digit(e.to)) && all_of_cps(e.from, num) && all_of_cps(e.to, num)) return true;
    // A separator changed between two digits: "18:30" -> "18.30" as ":" -> ".".
    const auto sep = [](char32_t c) { return c == U'.' || c == U',' || c == U':' || is_number_space(c); };
    return all_of_cps(e.from, sep) && all_of_cps(e.to, sep) && text::is_digit(cp_before(segment, e.start)) &&
           text::is_digit(cp_at(segment, e.end));
}

// from is a word written all in capitals (>= 2 letters) and to lowercases some of it.
bool lowercases_capitals(std::string_view from, std::string_view to) {
    int letters = 0;
    for (const char32_t c : decode_all(from)) {
        if (!text::is_letter(c)) continue;
        if (text::is_lower(c)) return false;
        if (text::is_upper(c)) ++letters;
    }
    if (letters < 2) return false;
    for (const char32_t c : decode_all(to))
        if (text::is_letter(c) && text::is_lower(c)) return true;
    return false;
}

bool touches_span(const diff::Edit& e, const protect::Span& s) {
    if (e.start == e.end) return s.start < e.start && e.start < s.end;  // insertion strictly inside
    return e.start < s.end && s.start < e.end;
}

}  // namespace

bool invalid_output(std::string_view segment, std::string_view output) {
    const std::string_view out = infer::trim_like_python(output);
    if (out.empty()) return true;
    if (out.find("<think>") != std::string_view::npos || out.find("</think>") != std::string_view::npos) return true;
    const QuotePair* q = wrapping_quotes(out);
    if (q == nullptr) return false;
    const QuotePair* had = wrapping_quotes(infer::trim_like_python(segment));
    return had == nullptr || had->open != q->open;
}

namespace {

// Levenshtein distance in code points: the characters an edit really changes («estas»→«estás»
// is 1, not 5).
std::size_t edit_distance(std::string_view a, std::string_view b) {
    auto cps = [](std::string_view s) {
        std::vector<char32_t> out;
        for (std::size_t i = 0; i < s.size();) {
            const text::Decoded d = text::decode(s, i);
            out.push_back(d.cp);
            i += d.len;
        }
        return out;
    };
    const std::vector<char32_t> x = cps(a), y = cps(b);
    std::vector<std::size_t> row(y.size() + 1);
    for (std::size_t j = 0; j <= y.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= x.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= y.size(); ++j) {
            const std::size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (x[i - 1] == y[j - 1] ? 0 : 1)});
            diag = up;
        }
    }
    return row[y.size()];
}

}  // namespace

double change_ratio(std::string_view segment, const std::vector<diff::Edit>& edits) {
    if (edits.empty()) return 0;
    std::size_t changed = 0;
    for (const diff::Edit& e : edits) changed += edit_distance(e.from, e.to);
    const std::size_t n = text::code_points(segment);
    if (n == 0) return std::numeric_limits<double>::infinity();
    return static_cast<double>(changed) / static_cast<double>(n);
}

bool out_of_catalog(const Item& item, std::string_view segment) {
    const diff::Edit& e = item.edit;
    const std::string_view from = e.from, to = e.to;
    // An added comma (only commas are removed in v1).
    if (to.find(',') != std::string_view::npos && from.find(',') == std::string_view::npos) return true;
    // An added final period.
    if (e.end == segment.size() && to.ends_with('.') && !from.ends_with('.')) return true;
    // Only the style of quotes or dashes, or quotes added or removed.
    if (from != to && canonical_style(from) == canonical_style(to)) return true;
    if ((from.empty() && only_quotes(to)) || (to.empty() && only_quotes(from))) return true;
    // Digits, number and time formatting.
    if (numeric_edit(e, segment)) return true;
    // Emphasis in capitals.
    if (lowercases_capitals(from, to)) return true;
    return catalog::admitted_variant(from, to);
}

Outcome apply(std::string_view segment, std::string_view output, std::vector<Item> items,
              const std::vector<protect::Span>& spans, const std::set<catalog::Type>& disabled,
              const Thresholds& thresholds) {
    Outcome o;
    // 1
    if (invalid_output(segment, output)) {
        o.rejection = Rejection::InvalidOutput;
        return o;
    }
    // 2
    std::vector<diff::Edit> edits;
    edits.reserve(items.size());
    for (const Item& it : items) edits.push_back(it.edit);
    if (change_ratio(segment, edits) > thresholds.max_change_ratio) {
        o.rejection = Rejection::ChangeRatio;
        return o;
    }
    // 3 and 4
    std::vector<Item> kept;
    int otro = 0;
    for (Item& it : items) {
        bool drop = false;
        for (const protect::Span& s : spans) drop = drop || touches_span(it.edit, s);
        if (drop || out_of_catalog(it, segment)) continue;
        if (it.result.type == catalog::Type::Otro) ++otro;
        kept.push_back(std::move(it));
    }
    // 5
    if (otro > thresholds.max_otro) {
        o.rejection = Rejection::TooManyOtro;
        return o;
    }
    // 6
    for (Item& it : kept)
        if (!disabled.contains(it.result.type)) o.kept.push_back(std::move(it));
    return o;
}

}  // namespace revisa::guard
