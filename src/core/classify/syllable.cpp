#include "core/classify/syllable.hpp"

#include <utility>

#include "core/text/text.hpp"

namespace revisa::classify {

namespace {

// One orthographic unit: a vowel letter, a consonant letter, or a digraph (ch, ll, rr, and qu/gu
// when the «u» is silent).
struct Unit {
    std::u32string text;
    bool vowel = false;
    char32_t v = 0;  // the vowel letter when vowel
};

bool is_vowel_letter(char32_t c) {
    switch (c) {
        case U'a': case U'e': case U'i': case U'o': case U'u':
        case U'á': case U'é': case U'í': case U'ó': case U'ú': case U'ü':
            return true;
        default:
            return false;
    }
}

bool is_consonant_letter(char32_t c) { return (c >= U'a' && c <= U'z' && !is_vowel_letter(c)) || c == U'ñ'; }

bool is_acute(char32_t c) { return c == U'á' || c == U'é' || c == U'í' || c == U'ó' || c == U'ú'; }

bool is_open(char32_t c) { return c == U'a' || c == U'e' || c == U'o' || c == U'á' || c == U'é' || c == U'ó'; }

char32_t base_vowel(char32_t c) {
    switch (c) {
        case U'á': return U'a';
        case U'é': return U'e';
        case U'í': return U'i';
        case U'ó': return U'o';
        case U'ú': case U'ü': return U'u';
        case U'y': return U'i';
        default: return c;
    }
}

bool is_e_or_i(char32_t c) { return c == U'e' || c == U'é' || c == U'i' || c == U'í'; }

// Two adjacent vowels in one syllable (orthographic convention).
bool diphthong(char32_t x, char32_t y) {
    if (base_vowel(x) == base_vowel(y)) return false;  // «chiita», «leer»
    const bool ox = is_open(x), oy = is_open(y);
    if (ox && oy) return false;
    if (ox || oy) {
        const char32_t closed = ox ? y : x;
        return closed != U'í' && closed != U'ú';  // «país», «baúl»: hiatus
    }
    return true;  // two different closed vowels: «cuida», «destruí»
}

bool inseparable(const std::u32string& a, const std::u32string& b) {
    if (a.size() != 1 || b.size() != 1) return false;
    const char32_t x = a[0], y = b[0];
    if (y == U'l' || y == U'r') {
        if (x == U'p' || x == U'b' || x == U'c' || x == U'g' || x == U'f' || x == U'k') return true;
        if (y == U'r' && (x == U'd' || x == U't')) return true;
    }
    return false;
}

std::optional<std::vector<Unit>> units_of(std::string_view word) {
    std::u32string cps;
    const std::string low = text::lower(word);
    for (std::size_t i = 0; i < low.size();) {
        const text::Decoded d = text::decode(low, i);
        if (!is_vowel_letter(d.cp) && !is_consonant_letter(d.cp) && d.cp != U'y') return std::nullopt;
        cps += d.cp;
        i += d.len;
    }
    if (cps.empty()) return std::nullopt;
    std::vector<Unit> out;
    const std::size_t n = cps.size();
    auto at = [&](std::size_t i) -> char32_t { return i < n ? cps[i] : 0; };
    for (std::size_t i = 0; i < n;) {
        const char32_t c = cps[i];
        const char32_t c1 = at(i + 1);
        if ((c == U'c' && c1 == U'h') || (c == U'l' && c1 == U'l') || (c == U'r' && c1 == U'r') ||
            (c == U'q' && c1 == U'u') || (c == U'g' && c1 == U'u' && is_e_or_i(at(i + 2)))) {
            out.push_back({std::u32string{c, c1}, false, 0});
            i += 2;
            continue;
        }
        if (c == U'y') {
            const bool vowel = i + 1 == n || !is_vowel_letter(c1);
            out.push_back({std::u32string{c}, vowel, vowel ? c : 0});
        } else if (is_vowel_letter(c)) {
            out.push_back({std::u32string{c}, true, c});
        } else {
            out.push_back({std::u32string{c}, false, 0});
        }
        ++i;
    }
    return out;
}

std::string utf8(const std::u32string& s) {
    std::string out;
    for (const char32_t c : s) text::append_utf8(out, c);
    return out;
}

}  // namespace

std::optional<Syllables> syllabify(std::string_view word) {
    const auto maybe_units = units_of(word);
    if (!maybe_units) return std::nullopt;
    const std::vector<Unit>& u = *maybe_units;
    const std::size_t n = u.size();

    // Nuclei: runs of vowels (an «h» between two vowels does not break them) split at hiatuses.
    std::vector<std::pair<std::size_t, std::size_t>> nuclei;  // [begin, end) over units
    for (std::size_t i = 0; i < n;) {
        if (!u[i].vowel) {
            ++i;
            continue;
        }
        std::size_t last = i;
        int open = is_open(u[i].v) ? 1 : 0;
        for (;;) {
            std::size_t k = n;
            if (last + 1 < n && u[last + 1].vowel) k = last + 1;
            else if (last + 2 < n && u[last + 1].text == U"h" && u[last + 2].vowel) k = last + 2;
            if (k == n || !diphthong(u[last].v, u[k].v)) break;
            if (is_open(u[k].v) && open > 0) break;
            open += is_open(u[k].v) ? 1 : 0;
            last = k;
        }
        nuclei.emplace_back(i, last + 1);
        i = last + 1;
    }
    if (nuclei.empty()) return std::nullopt;

    // Syllable boundaries: one consonant goes to the next syllable; of two or more, the last one
    // does, or the last two if they form an inseparable onset.
    std::vector<std::size_t> starts{0};
    for (std::size_t j = 1; j < nuclei.size(); ++j) {
        const std::size_t prev_end = nuclei[j - 1].second, next_begin = nuclei[j].first;
        const std::size_t m = next_begin - prev_end;
        std::size_t s = prev_end;
        if (m >= 2) s = inseparable(u[next_begin - 2].text, u[next_begin - 1].text) ? next_begin - 2 : next_begin - 1;
        starts.push_back(s);
    }
    starts.push_back(n);

    Syllables out;
    std::size_t accent_unit = n;
    for (std::size_t i = 0; i < n; ++i) {
        if (u[i].vowel) ++out.vowels;
        if (u[i].vowel && is_acute(u[i].v) && accent_unit == n) accent_unit = i;
    }
    for (std::size_t j = 0; j + 1 < starts.size(); ++j) {
        std::u32string syl;
        for (std::size_t i = starts[j]; i < starts[j + 1]; ++i) syl += u[i].text;
        out.parts.push_back(utf8(syl));
        if (accent_unit >= starts[j] && accent_unit < starts[j + 1]) out.stressed = j;
    }

    // Ending, as the accent rules see it.
    const Unit& last = u.back();
    const bool prev_vowel = n >= 2 && u[n - 2].vowel && u[n - 2].v != U'y';
    if (last.text == U"y") out.ending = Ending::Y;
    else if (last.vowel) out.ending = Ending::Vowel;
    else if (n >= 2 && !u[n - 2].vowel) out.ending = Ending::Cluster;
    else if (last.text == U"n" && prev_vowel) out.ending = Ending::N;
    else if (last.text == U"s" && prev_vowel) out.ending = Ending::S;
    else if (prev_vowel || n == 1) out.ending = Ending::Consonant;
    else out.ending = Ending::Cluster;  // after a vowel «y»: «jerseys»

    const std::size_t k = out.parts.size();
    out.tilde = accent_unit < n;
    if (!out.tilde) {
        const bool llana = out.ending == Ending::Vowel || out.ending == Ending::N || out.ending == Ending::S;
        out.stressed = (k >= 2 && llana) ? k - 2 : k - 1;
    } else {
        const char32_t v = u[accent_unit].v;
        if (v == U'í' || v == U'ú') {
            auto open_at = [&](std::size_t i) { return i < n && u[i].vowel && is_open(u[i].v); };
            const std::size_t a = accent_unit;
            out.hiatus_tilde = open_at(a + 1) || (a + 2 < n && u[a + 1].text == U"h" && open_at(a + 2)) ||
                               (a >= 1 && open_at(a - 1)) || (a >= 2 && u[a - 1].text == U"h" && open_at(a - 2));
        }
    }
    const std::size_t from_end = k - 1 - out.stressed;
    if (k == 1) out.stress = Stress::Monosyllable;
    else if (from_end == 0) out.stress = Stress::Aguda;
    else if (from_end == 1) out.stress = Stress::Llana;
    else if (from_end == 2) out.stress = Stress::Esdrujula;
    else out.stress = Stress::Sobresdrujula;
    return out;
}

std::string_view stress_name(Stress s) {
    switch (s) {
        case Stress::Monosyllable: return "monosilaba";
        case Stress::Aguda: return "aguda";
        case Stress::Llana: return "llana";
        case Stress::Esdrujula: return "esdrujula";
        case Stress::Sobresdrujula: return "sobresdrujula";
    }
    return "";
}

std::string join_syllables(const Syllables& s) {
    std::string out;
    for (std::size_t i = 0; i < s.parts.size(); ++i) {
        if (i) out += '-';
        out += s.parts[i];
    }
    return out;
}

}  // namespace revisa::classify
