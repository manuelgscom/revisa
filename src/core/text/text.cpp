#include "core/text/text.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace revisa::text {

namespace {

#include "core/text/nfc_table.inc"

template <typename T, std::size_t N>
const T* find_cp(const T (&table)[N], char32_t cp) {
    const T* it = std::lower_bound(std::begin(table), std::end(table), cp,
                                   [](const T& e, char32_t c) { return e.cp < c; });
    return it != std::end(table) && it->cp == cp ? it : nullptr;
}

unsigned ccc(char32_t cp) {
    const CccEntry* e = find_cp(kCcc, cp);
    return e ? e->ccc : 0;
}

char32_t compose(char32_t a, char32_t b) {
    const ComposeEntry* it = std::lower_bound(
        std::begin(kCompose), std::end(kCompose), std::pair{a, b},
        [](const ComposeEntry& e, const std::pair<char32_t, char32_t>& k) {
            return e.first < k.first || (e.first == k.first && e.second < k.second);
        });
    return it != std::end(kCompose) && it->first == a && it->second == b ? it->cp : 0;
}

// Full canonical decomposition of cp (recursive over the table).
void decompose(char32_t cp, std::vector<char32_t>& out) {
    if (const DecompEntry* e = find_cp(kDecomp, cp)) {
        decompose(e->first, out);
        if (e->second) decompose(e->second, out);
        return;
    }
    out.push_back(cp);
}

// true if the full decomposition of cp starts with a starter (ccc 0).
bool starts_with_starter(char32_t cp) {
    while (const DecompEntry* e = find_cp(kDecomp, cp)) cp = e->first;
    return ccc(cp) == 0;
}

}  // namespace

// --- UTF-8 ----------------------------------------------------------------------------------

Decoded decode(std::string_view s, std::size_t i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) return {b0, 1};
    std::size_t n = 0;
    char32_t cp = 0;
    char32_t min = 0;
    if ((b0 & 0xE0) == 0xC0) {
        n = 2;
        cp = b0 & 0x1Fu;
        min = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        n = 3;
        cp = b0 & 0x0Fu;
        min = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
        n = 4;
        cp = b0 & 0x07u;
        min = 0x10000;
    } else {
        return {kReplacement, 1};
    }
    if (i + n > s.size()) return {kReplacement, 1};
    for (std::size_t k = 1; k < n; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) return {kReplacement, 1};
        cp = (cp << 6) | (b & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return {kReplacement, 1};
    return {cp, n};
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::size_t code_points(std::string_view s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); i += decode(s, i).len) ++n;
    return n;
}

std::size_t utf16_length(std::string_view s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size();) {
        const Decoded d = decode(s, i);
        n += d.cp >= 0x10000 ? 2 : 1;
        i += d.len;
    }
    return n;
}

Utf16Index::Utf16Index(std::string_view text) : units_(text.size() + 1, 0) {
    std::size_t u = 0;
    for (std::size_t i = 0; i < text.size();) {
        const Decoded d = decode(text, i);
        for (std::size_t k = 0; k < d.len; ++k) units_[i + k] = u;
        u += d.cp >= 0x10000 ? 2 : 1;
        i += d.len;
    }
    units_[text.size()] = u;
}

std::size_t Utf16Index::at(std::size_t byte) const {
    return units_[std::min(byte, units_.size() - 1)];
}

// --- Classes --------------------------------------------------------------------------------

bool is_space(char32_t cp) {
    return (cp >= 0x09 && cp <= 0x0D) || (cp >= 0x1C && cp <= 0x20) || cp == 0x85 || cp == 0xA0 ||
           cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 ||
           cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

bool is_mark(char32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F);
}

bool is_letter(char32_t cp) {
    if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    if (cp == 0xAA || cp == 0xB5 || cp == 0xBA) return true;
    if (cp >= 0xC0 && cp <= 0x24F) return cp != 0xD7 && cp != 0xF7;
    if (cp >= 0x1E00 && cp <= 0x1EFF) return true;
    if (cp >= 0x0386 && cp <= 0x03FF) return cp != 0x0387 && cp != 0x03F6;  // Greek
    if (cp >= 0x0400 && cp <= 0x0481) return true;                         // Cyrillic
    if (cp >= 0x048A && cp <= 0x04FF) return true;
    return false;
}

bool is_digit(char32_t cp) { return cp >= '0' && cp <= '9'; }

// --- Case -----------------------------------------------------------------------------------

char32_t to_lower(char32_t cp) {
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
    if (cp >= 0x100 && cp <= 0x137) return cp | 1;
    if (cp >= 0x139 && cp <= 0x148) return (cp & 1) ? cp + 1 : cp;
    if (cp >= 0x14A && cp <= 0x177) return cp | 1;
    if (cp == 0x178) return 0xFF;
    if (cp >= 0x179 && cp <= 0x17E) return (cp & 1) ? cp + 1 : cp;
    if (cp >= 0x1E00 && cp <= 0x1E95) return cp | 1;
    if (cp >= 0x1EA0 && cp <= 0x1EFF) return cp | 1;
    if (cp >= 0x391 && cp <= 0x3AB && cp != 0x3A2) return cp + 32;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 32;
    if (cp >= 0x400 && cp <= 0x40F) return cp + 80;
    return cp;
}

char32_t to_upper(char32_t cp) {
    if (cp < 0x80) return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32;
    if (cp == 0xFF) return 0x178;
    if (cp >= 0x100 && cp <= 0x137) return cp & ~char32_t{1};
    if (cp >= 0x139 && cp <= 0x148) return (cp & 1) ? cp : cp - 1;
    if (cp >= 0x14A && cp <= 0x177) return cp & ~char32_t{1};
    if (cp >= 0x179 && cp <= 0x17E) return (cp & 1) ? cp : cp - 1;
    if (cp >= 0x1E00 && cp <= 0x1E95) return cp & ~char32_t{1};
    if (cp >= 0x1EA0 && cp <= 0x1EFF) return cp & ~char32_t{1};
    if (cp >= 0x3B1 && cp <= 0x3CB && cp != 0x3C2) return cp - 32;
    if (cp >= 0x430 && cp <= 0x44F) return cp - 32;
    if (cp >= 0x450 && cp <= 0x45F) return cp - 80;
    return cp;
}

bool is_upper(char32_t cp) { return to_lower(cp) != cp; }
bool is_lower(char32_t cp) { return to_upper(cp) != cp; }

namespace {

template <typename F>
std::string map_cps(std::string_view s, F&& f) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const Decoded d = decode(s, i);
        if (d.cp == kReplacement && d.len == 1) {
            out += s[i];  // keep invalid bytes as they are
        } else {
            f(out, d.cp);
        }
        i += d.len;
    }
    return out;
}

}  // namespace

std::string lower(std::string_view s) {
    return map_cps(s, [](std::string& out, char32_t cp) { append_utf8(out, to_lower(cp)); });
}

std::string upper(std::string_view s) {
    return map_cps(s, [](std::string& out, char32_t cp) { append_utf8(out, to_upper(cp)); });
}

std::string strip_accents(std::string_view s) {
    return map_cps(s, [](std::string& out, char32_t cp) {
        if (is_mark(cp)) return;
        if (cp == 0xF1 || cp == 0xD1) {
            append_utf8(out, cp);
            return;
        }
        std::vector<char32_t> d;
        decompose(cp, d);
        append_utf8(out, d.front());
    });
}

std::string fold(std::string_view s) { return lower(strip_accents(s)); }

// --- NFC ------------------------------------------------------------------------------------

std::pair<std::size_t, std::size_t> Normalized::map(std::size_t start, std::size_t end) const {
    const std::size_t n = text.size();
    if (start >= n) {
        return {orig_size, orig_size};
    }
    const std::size_t s = orig_start[start];
    if (end <= start) return {s, s};
    const std::size_t e = end >= n ? (n ? orig_end[n - 1] : 0) : orig_end[end - 1];
    return {s, std::max(s, e)};
}

Normalized nfc(std::string_view s) {
    struct Cp {
        char32_t cp;
        std::size_t start, len;
    };
    std::vector<Cp> cps;
    for (std::size_t i = 0; i < s.size();) {
        const Decoded d = decode(s, i);
        cps.push_back({d.cp, i, d.len});
        i += d.len;
    }

    Normalized out;
    out.orig_size = s.size();
    out.text.reserve(s.size());
    auto emit_identity = [&](const Cp& c) {
        const std::size_t before = out.text.size();
        out.text.append(s.substr(c.start, c.len));  // verbatim, invalid bytes included
        for (std::size_t k = before; k < out.text.size(); ++k) {
            out.orig_start.push_back(c.start);
            out.orig_end.push_back(c.start + c.len);
        }
    };

    std::size_t i = 0;
    while (i < cps.size()) {
        // Cluster: one code point plus the following ones whose decomposition has no starter.
        std::size_t j = i + 1;
        while (j < cps.size() && !starts_with_starter(cps[j].cp)) ++j;

        std::vector<char32_t> d;
        for (std::size_t k = i; k < j; ++k) decompose(cps[k].cp, d);
        // Canonical ordering of the marks after the (optional) starter.
        const std::size_t first_mark = ccc(d.front()) == 0 ? 1 : 0;
        std::stable_sort(d.begin() + static_cast<std::ptrdiff_t>(first_mark), d.end(),
                         [](char32_t a, char32_t b) { return ccc(a) < ccc(b); });
        // Composition with the starter; a mark is blocked by a kept mark of the same class.
        std::vector<char32_t> r;
        if (first_mark == 1) {
            r.push_back(d.front());
            unsigned last = 0;
            for (std::size_t k = 1; k < d.size(); ++k) {
                const unsigned c = ccc(d[k]);
                const char32_t comp = last < c ? compose(r.front(), d[k]) : 0;
                if (comp) {
                    r.front() = comp;
                } else {
                    r.push_back(d[k]);
                    last = c;
                }
            }
        } else {
            r = d;
        }

        bool same = r.size() == j - i;
        for (std::size_t k = 0; same && k < r.size(); ++k) same = r[k] == cps[i + k].cp;
        if (same) {
            for (std::size_t k = i; k < j; ++k) emit_identity(cps[k]);
        } else {
            const std::size_t cs = cps[i].start, ce = cps[j - 1].start + cps[j - 1].len;
            const std::size_t before = out.text.size();
            for (const char32_t c : r) append_utf8(out.text, c);
            for (std::size_t k = before; k < out.text.size(); ++k) {
                out.orig_start.push_back(cs);
                out.orig_end.push_back(ce);
            }
        }
        i = j;
    }
    return out;
}

std::string to_nfc(std::string_view s) { return nfc(s).text; }

}  // namespace revisa::text
