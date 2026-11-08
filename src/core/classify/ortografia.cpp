// Spelling rules of the classifier: types 1-20 of the catalog.
// Tildes and homophones compare without case. Closed lists live in
// src/core/catalog/ (pares/*.tsv, interrogativos.txt, dias_meses.txt).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/catalog/dictionary.hpp"
#include "core/classify/rules.hpp"
#include "core/classify/syllable.hpp"
#include "core/text/text.hpp"

namespace revisa::classify::rules {

namespace {

using catalog::Type;

std::u32string cps(std::string_view s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const text::Decoded d = text::decode(s, i);
        out += d.cp;
        i += d.len;
    }
    return out;
}

std::string utf8(std::u32string_view s) {
    std::string out;
    for (const char32_t c : s) text::append_utf8(out, c);
    return out;
}

bool only_space(std::string_view s) {
    for (const char32_t c : cps(s))
        if (!text::is_space(c)) return false;
    return true;
}

// One word: not empty, no spaces, at least one letter.
bool one_word(std::string_view s) {
    if (s.empty()) return false;
    bool letter = false;
    for (const char32_t c : cps(s)) {
        if (text::is_space(c)) return false;
        letter = letter || text::is_letter(c);
    }
    return letter;
}

// Lowercase, trimmed, with every run of spaces as one ' ': the key of multi-word pairs.
std::string phrase(std::string_view s) {
    std::string out;
    bool space = false;
    for (const char32_t c : cps(trim(s))) {
        if (text::is_space(c)) {
            space = true;
            continue;
        }
        if (space) out += ' ';
        space = false;
        text::append_utf8(out, text::to_lower(c));
    }
    return out;
}

// Lowercase without spaces or hyphens (JUNTO_SEPARADO).
std::string joined(std::string_view s) {
    std::string out;
    for (const char32_t c : cps(s))
        if (!text::is_space(c) && c != U'-') text::append_utf8(out, text::to_lower(c));
    return out;
}

bool has_separator(std::string_view s) {
    for (const char32_t c : cps(s))
        if (text::is_space(c) || c == U'-') return true;
    return false;
}

bool has_space(std::string_view s) {
    for (const char32_t c : cps(s))
        if (text::is_space(c)) return true;
    return false;
}

enum class Key { Lower, Fold };

std::string key_of(std::string_view s, Key k) { return k == Key::Lower ? phrase(s) : text::fold(phrase(s)); }

// Explanation of the pair de -> a in pares/<file> (both directions if both_ways).
std::optional<std::string> pair_explanation(std::string_view file, std::string_view de, std::string_view a,
                                            bool both_ways, Key k = Key::Lower) {
    const std::string kd = key_of(de, k), ka = key_of(a, k);
    for (const catalog::Row& r : catalog::rows(file)) {
        if (r.cols.size() < 3) continue;
        const std::string c0 = key_of(r.cols[0], k), c1 = key_of(r.cols[1], k);
        if ((c0 == kd && c1 == ka) || (both_ways && c0 == ka && c1 == kd)) return std::string(r.cols[2]);
    }
    return std::nullopt;
}

// true if the one-column list file has word (compared with text::fold).
bool in_list(std::string_view file, std::string_view word) {
    const std::string k = text::fold(word);
    for (const catalog::Row& r : catalog::rows(file))
        if (!r.cols.empty() && text::fold(r.cols[0]) == k) return true;
    return false;
}

// Text of a {regla}: rows "REGLA <TAB> variant <TAB> text" of explicaciones/*.tsv.
std::string_view regla(std::string_view variant) {
    for (const std::string_view file : catalog::data_files()) {
        if (!file.starts_with("explicaciones/")) continue;
        for (const catalog::Row& r : catalog::rows(file))
            if (r.cols.size() == 3 && r.cols[0] == "REGLA" && r.cols[1] == variant) return r.cols[2];
    }
    std::fprintf(stderr, "revisa: missing explanation REGLA/%.*s\n", static_cast<int>(variant.size()), variant.data());
    std::abort();
}

// First letter of s (0 if none).
char32_t first_letter(std::string_view s) {
    for (const char32_t c : cps(s))
        if (text::is_letter(c)) return c;
    return 0;
}

bool lower_to_upper(std::string_view from, std::string_view to) {
    return text::is_lower(first_letter(from)) && text::is_upper(first_letter(to));
}

bool is_acute(char32_t c) {
    return c == U'á' || c == U'é' || c == U'í' || c == U'ó' || c == U'ú';
}

// Accent differences of two words with the same fold: positions where one has an acute accent
// and the other does not.
struct TildeDiff {
    bool to_extra = false;    // to has an accent from lacks
    bool from_extra = false;  // from has an accent to lacks
    std::size_t from_pos = 0, to_pos = 0;
};

std::optional<TildeDiff> tilde_diff(const Input& in) {
    const std::string_view f = trim(in.from), t = trim(in.to);
    if (!one_word(f) || !one_word(t) || in.from_fold != in.to_fold || in.from_lower == in.to_lower) return std::nullopt;
    const std::u32string a = cps(text::lower(f)), b = cps(text::lower(t));
    if (a.size() != b.size()) return std::nullopt;
    TildeDiff d;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (is_acute(b[i]) && !is_acute(a[i]) && !d.to_extra) {
            d.to_extra = true;
            d.to_pos = i;
        }
        if (is_acute(a[i]) && !is_acute(b[i]) && !d.from_extra) {
            d.from_extra = true;
            d.from_pos = i;
        }
    }
    if (!d.to_extra && !d.from_extra) return std::nullopt;  // only a diaeresis changed
    return d;
}

// Adverb in -mente whose accent (at pos) is in the adjective: «rápidamente».
bool mente_adverb(std::string_view word, std::size_t accent_pos) {
    const std::u32string w = cps(text::lower(word));
    constexpr std::u32string_view kMente = U"mente";
    return w.size() >= kMente.size() + 4 && std::u32string_view(w).ends_with(kMente) &&
           accent_pos < w.size() - kMente.size();
}

// Optimal string alignment distance (Damerau-Levenshtein with adjacent transpositions).
std::size_t osa_distance(const std::u32string& a, const std::u32string& b) {
    const std::size_t n = a.size(), m = b.size();
    std::vector<std::vector<std::size_t>> d(n + 1, std::vector<std::size_t>(m + 1));
    for (std::size_t i = 0; i <= n; ++i) d[i][0] = i;
    for (std::size_t j = 0; j <= m; ++j) d[0][j] = j;
    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    }
    return d[n][m];
}

// The folded code points of two one-word edits (nullopt if either is not one word).
std::optional<std::pair<std::u32string, std::u32string>> folded_words(const Input& in) {
    if (!one_word(trim(in.from)) || !one_word(trim(in.to))) return std::nullopt;
    return std::make_pair(cps(text::fold(trim(in.from))), cps(text::fold(trim(in.to))));
}

// Index of the only differing code point of two strings of the same length.
std::optional<std::size_t> single_substitution(const std::u32string& a, const std::u32string& b) {
    if (a.size() != b.size()) return std::nullopt;
    std::optional<std::size_t> pos;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) continue;
        if (pos) return std::nullopt;
        pos = i;
    }
    return pos;
}

std::string quoted(std::u32string_view letters) { return "«" + utf8(letters) + "»"; }

Result with_explanation(Type type, std::string text) { return Result{type, std::move(text)}; }

}  // namespace

// 1. Only whitespace changes.
std::optional<Result> espaciado(const Input& in) {
    if (!only_space(in.from) || !only_space(in.to)) return std::nullopt;
    return make(Type::Espaciado);
}

// 2. Same text except for case.
std::optional<Result> mayuscula(const Input& in) {
    if (in.from_lower != in.to_lower || first_letter(in.from) == 0) return std::nullopt;
    const std::string_view t = trim(in.to);
    const bool day_or_month = in_list("dias_meses.txt", phrase(t));
    const char32_t ff = first_letter(in.from), ft = first_letter(in.to);
    if (text::is_lower(ff) && text::is_upper(ft)) {
        if (in.sentence_start) return make(Type::Mayuscula, "inicio");
        if (day_or_month) return std::nullopt;  // the norm asks for the opposite
        return make(Type::Mayuscula, "nombre_propio");
    }
    if (text::is_upper(ff) && text::is_lower(ft)) {
        if (in.sentence_start) return std::nullopt;
        if (day_or_month) return make(Type::Mayuscula, "dias_meses");
        return make(Type::Mayuscula, "minuscula", {{"a", t}});
    }
    // Same case on the first letter: only a lowering inside the word has an explanation.
    auto uppers = [](std::string_view s) {
        std::size_t n = 0;
        for (const char32_t c : cps(s)) n += text::is_upper(c) ? 1 : 0;
        return n;
    };
    if (uppers(in.to) < uppers(in.from)) return make(Type::Mayuscula, "minuscula", {{"a", t}});
    return std::nullopt;
}

// 3. Diacritic tilde of the closed list.
std::optional<Result> tilde_diacritica(const Input& in) {
    if (!tilde_diff(in)) return std::nullopt;
    if (auto e = pair_explanation("pares/TILDE_DIACRITICA.tsv", in.from, in.to, true))
        return with_explanation(Type::TildeDiacritica, std::move(*e));
    return std::nullopt;
}

// 4. Interrogative or exclamative tilde. «por que» -> «por qué» is POR_QUE.
std::optional<Result> tilde_interrogativa(const Input& in) {
    const auto d = tilde_diff(in);
    if (!d || !d->to_extra) return std::nullopt;
    if (text::fold(in.prev_word) == "por" && in.from_fold == "que") return std::nullopt;
    // The list has the accented forms: compare the exact lowercase form, not the fold.
    const std::string to = phrase(in.to);
    bool listed = false;
    for (const catalog::Row& r : catalog::rows("interrogativos.txt"))
        listed = listed || (!r.cols.empty() && r.cols[0] == to);
    if (!listed) return std::nullopt;
    const std::string_view t = trim(in.to);
    return make(Type::TildeInterrogativa, lower_to_upper(in.from, in.to) ? "con_mayuscula" : "", {{"a", t}});
}

// 5. porque / por qué / porqué / por que.
std::optional<Result> por_que(const Input& in) {
    std::string f = phrase(in.from), t = phrase(in.to);
    if (text::fold(in.prev_word) == "por" && text::fold(f) == "que" && text::fold(t) == "que") {
        f = "por " + f;
        t = "por " + t;
    }
    if (auto e = pair_explanation("pares/POR_QUE.tsv", f, t, true)) return with_explanation(Type::PorQue, std::move(*e));
    return std::nullopt;
}

// 6. The correction adds a tilde.
std::optional<Result> tilde_falta(const Input& in) {
    const auto d = tilde_diff(in);
    if (!d || !d->to_extra) return std::nullopt;
    const std::string_view t = trim(in.to);
    const auto s = syllabify(t);
    std::string_view variant;
    if (!s) {
        variant = {};
    } else if (mente_adverb(t, d->to_pos)) {
        variant = "mente";
    } else if (s->hiatus_tilde) {
        variant = "hiato";
    } else {
        switch (s->stress) {
            case Stress::Aguda:
                if (s->ending == Ending::Vowel) variant = "aguda_vocal";
                else if (s->ending == Ending::N) variant = "aguda_n";
                else if (s->ending == Ending::S) variant = "aguda_s";
                break;
            case Stress::Llana:
                if (s->ending == Ending::Consonant) variant = "llana";
                else if (s->ending == Ending::Y) variant = "llana_y";
                else if (s->ending == Ending::Cluster) variant = "llana_grupo";
                break;
            case Stress::Esdrujula: variant = "esdrujula"; break;
            case Stress::Sobresdrujula: variant = "sobresdrujula"; break;
            case Stress::Monosyllable: break;
        }
    }
    if (variant.empty()) return make(Type::TildeFalta, "sin_regla", {{"a", t}});
    return make(Type::TildeFalta, "", {{"a", t}, {"regla", regla(variant)}});
}

// 7. The correction removes a tilde.
std::optional<Result> tilde_sobra(const Input& in) {
    const auto d = tilde_diff(in);
    if (!d || !d->from_extra || d->to_extra) return std::nullopt;
    const std::string_view t = trim(in.to);
    const auto s = syllabify(t);
    std::string_view variant;
    if (!s) {
        variant = {};
    } else if (mente_adverb(t, d->from_pos)) {
        variant = "no_mente";
    } else if (!s->tilde) {
        switch (s->stress) {
            case Stress::Monosyllable:
                variant = s->vowels > 1 ? "no_monosilabo_diptongo" : "no_monosilabo";
                break;
            case Stress::Aguda:
                if (s->ending == Ending::Consonant) variant = "no_aguda";
                else if (s->ending == Ending::Y) variant = "no_aguda_y";
                else if (s->ending == Ending::Cluster) variant = "no_aguda_grupo";
                break;
            case Stress::Llana:
                if (s->ending == Ending::Vowel || s->ending == Ending::N || s->ending == Ending::S) variant = "no_llana";
                break;
            default: break;
        }
    }
    if (variant.empty()) return make(Type::TildeSobra, "sin_regla", {{"a", t}});
    return make(Type::TildeSobra, "", {{"a", t}, {"regla", regla(variant)}});
}

// 8. haber / a ver / a haber / aver. «a haber» -> «a ver» arrives as «haber» -> «ver» after «a».
std::optional<Result> haber_a_ver(const Input& in) {
    std::string f = phrase(in.from), t = phrase(in.to);
    if (text::fold(in.prev_word) == "a" && ((f == "haber" && t == "ver") || (f == "ver" && t == "haber"))) {
        f = "a " + f;
        t = "a " + t;
    }
    if (auto e = pair_explanation("pares/HABER_A_VER.tsv", f, t, true))
        return with_explanation(Type::HaberAVer, std::move(*e));
    return std::nullopt;
}

// 9. a/ha, e/he, as/has, an/han. Adding the h uses the type's explanation; removing it, the
// directional rows of pares/VERBO_HABER_H.tsv.
std::optional<Result> verbo_haber_h(const Input& in) {
    const std::string f = phrase(in.from), t = phrase(in.to);
    if ((f == "a" && t == "ha") || (f == "e" && t == "he") || (f == "as" && t == "has") || (f == "an" && t == "han"))
        return make(Type::VerboHaberH, "", {{"a", t}});
    if (auto e = pair_explanation("pares/VERBO_HABER_H.tsv", f, t, false))
        return with_explanation(Type::VerboHaberH, std::move(*e));
    return std::nullopt;
}

// 10. Forms of «echar» and «hacer», «desechar» and «deshacer».
std::optional<Result> echo_hecho(const Input& in) {
    if (!one_word(trim(in.from)) || !one_word(trim(in.to))) return std::nullopt;
    if (auto e = pair_explanation("pares/ECHO_HECHO.tsv", in.from, in.to, true))
        return with_explanation(Type::EchoHecho, std::move(*e));
    return std::nullopt;
}

// 11. hay / ahí / ay / ai (compared without accents, so «ahi» counts as «ahí»).
std::optional<Result> hay_ahi_ay(const Input& in) {
    if (!one_word(trim(in.from)) || !one_word(trim(in.to))) return std::nullopt;
    if (auto e = pair_explanation("pares/HAY_AHI_AY.tsv", in.from, in.to, true, Key::Fold))
        return with_explanation(Type::HayAhiAy, std::move(*e));
    return std::nullopt;
}

// 12. sino / si no.
std::optional<Result> sino_si_no(const Input& in) {
    const std::string f = phrase(in.from), t = phrase(in.to);
    if ((f == "sino" && t == "si no") || (f == "si no" && t == "sino"))
        return make(Type::SinoSiNo, "", {{"a", trim(in.to)}});
    return std::nullopt;
}

// 13. Equal without spaces and hyphens, or a known pair of pares/JUNTO_SEPARADO.tsv.
std::optional<Result> junto_separado(const Input& in) {
    const std::string_view f = trim(in.from), t = trim(in.to);
    if (f.empty() || t.empty()) return std::nullopt;
    if (auto e = pair_explanation("pares/JUNTO_SEPARADO.tsv", f, t, true))
        return with_explanation(Type::JuntoSeparado, std::move(*e));
    // Only separators may differ: a change of case alone is MAYUSCULA (or nothing).
    if (joined(f) != joined(t) || joined(f).empty() || phrase(f) == phrase(t)) return std::nullopt;
    if (!has_separator(t)) return make(Type::JuntoSeparado, "junto", {{"a", t}});
    if (!has_separator(f) && has_space(t) && t.find('-') == std::string_view::npos)
        return make(Type::JuntoSeparado, "separado", {{"a", t}});
    return make(Type::JuntoSeparado, "", {{"a", t}});
}

// 14. One b <-> v substitution.
std::optional<Result> b_v(const Input& in) {
    const auto w = folded_words(in);
    if (!w) return std::nullopt;
    const auto& [a, b] = *w;
    const auto i = single_substitution(a, b);
    if (!i) return std::nullopt;
    const char32_t x = a[*i], y = b[*i];
    if (!((x == U'b' && y == U'v') || (x == U'v' && y == U'b'))) return std::nullopt;
    return make(Type::BV, "", {{"a", trim(in.to)}, {"letra", quoted(std::u32string(1, y))}});
}

// 15. One g <-> j substitution before e or i.
std::optional<Result> g_j(const Input& in) {
    const auto w = folded_words(in);
    if (!w) return std::nullopt;
    const auto& [a, b] = *w;
    const auto i = single_substitution(a, b);
    if (!i) return std::nullopt;
    const char32_t x = a[*i], y = b[*i];
    if (!((x == U'g' && y == U'j') || (x == U'j' && y == U'g'))) return std::nullopt;
    if (*i + 1 >= b.size() || (b[*i + 1] != U'e' && b[*i + 1] != U'i')) return std::nullopt;
    return make(Type::GJ, "", {{"a", trim(in.to)}, {"letra", quoted(std::u32string(1, y))}});
}

// 16. Only one «h» is added or removed (not the «h» of «ch»).
std::optional<Result> h(const Input& in) {
    const auto w = folded_words(in);
    if (!w) return std::nullopt;
    const auto& [a, b] = *w;
    if (a.size() + 1 != b.size() && b.size() + 1 != a.size()) return std::nullopt;
    const bool added = b.size() > a.size();
    const std::u32string& longer = added ? b : a;
    const std::u32string& shorter = added ? a : b;
    for (std::size_t p = 0; p < longer.size(); ++p) {
        if (longer[p] != U'h' || (p > 0 && longer[p - 1] == U'c')) continue;
        if (longer.substr(0, p) + longer.substr(p + 1) == shorter)
            return make(Type::H, "", {{"a", trim(in.to)}, {"con/sin", added ? "con" : "sin"}});
    }
    return std::nullopt;
}

// 17. One ll <-> y substitution.
std::optional<Result> ll_y(const Input& in) {
    const auto w = folded_words(in);
    if (!w) return std::nullopt;
    const auto& [a, b] = *w;
    auto swaps = [](const std::u32string& with_ll, const std::u32string& with_y) {
        for (std::size_t p = 0; p + 1 < with_ll.size(); ++p)
            if (with_ll[p] == U'l' && with_ll[p + 1] == U'l' &&
                with_ll.substr(0, p) + U"y" + with_ll.substr(p + 2) == with_y)
                return true;
        return false;
    };
    if (swaps(a, b)) return make(Type::LlY, "", {{"a", trim(in.to)}, {"letra", "«y»"}});
    if (swaps(b, a)) return make(Type::LlY, "", {{"a", trim(in.to)}, {"letra", "«ll»"}});
    return std::nullopt;
}

// 18. One substitution among c, z, s and x, or sc <-> c / s. A «c» only counts where it sounds
// like «z» or «s», before e or i («hize» -> «hice», «picina» -> «piscina»; not «ecribir»).
std::optional<Result> c_z_s_x(const Input& in) {
    const auto w = folded_words(in);
    if (!w) return std::nullopt;
    const auto& [a, b] = *w;
    auto czsx = [](char32_t c) { return c == U'c' || c == U'z' || c == U's' || c == U'x'; };
    auto before_e_i = [](const std::u32string& s, std::size_t p) {
        return p + 1 < s.size() && (s[p + 1] == U'e' || s[p + 1] == U'i');
    };
    if (const auto i = single_substitution(a, b)) {
        if (czsx(a[*i]) && czsx(b[*i]) && ((a[*i] != U'c' && b[*i] != U'c') || before_e_i(a, *i)))
            return make(Type::CZSX, "", {{"a", trim(in.to)}, {"letra", quoted(std::u32string(1, b[*i]))}});
        return std::nullopt;
    }
    if (a.size() + 1 != b.size() && b.size() + 1 != a.size()) return std::nullopt;
    const bool added = b.size() > a.size();
    const std::u32string& longer = added ? b : a;
    const std::u32string& shorter = added ? a : b;
    for (std::size_t p = 0; p < longer.size(); ++p) {
        const bool sc = (longer[p] == U's' && p + 1 < longer.size() && longer[p + 1] == U'c' &&
                         before_e_i(longer, p + 1)) ||
                        (longer[p] == U'c' && p > 0 && longer[p - 1] == U's' && before_e_i(longer, p));
        if (!sc || longer.substr(0, p) + longer.substr(p + 1) != shorter) continue;
        const std::string_view letra = added ? "«sc»" : (longer[p] == U's' ? "«c»" : "«s»");
        return make(Type::CZSX, "", {{"a", trim(in.to)}, {"letra", letra}});
    }
    return std::nullopt;
}

// 19. Only an opening «¿» or «¡» is added.
std::optional<Result> signo_apertura(const Input& in) {
    if (!only_space(in.from)) return std::nullopt;
    const std::string_view t = trim(in.to);
    if (t != "¿" && t != "¡") return std::nullopt;
    return make(Type::SignoApertura);
}

// 20. A typo: one word, distance <= 2, «de» not in the dictionary and «a» in it. Runs last.
std::optional<Result> errata(const Input& in) {
    const std::string_view f = trim(in.from), t = trim(in.to);
    if (!one_word(f) || !one_word(t)) return std::nullopt;
    if (osa_distance(cps(text::lower(f)), cps(text::lower(t))) > 2) return std::nullopt;
    if (catalog::in_dictionary(f) || !catalog::in_dictionary(t)) return std::nullopt;
    return make(Type::Errata, "", {{"de", f}, {"a", t}});
}

}  // namespace revisa::classify::rules
