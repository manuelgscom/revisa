// Grammar rules of the classifier: types 21-31. Each one looks only at the edit
// and its neighbouring words; the catalog order resolves overlaps (rules.hpp).
#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "core/catalog/dictionary.hpp"
#include "core/classify/rules.hpp"
#include "core/text/text.hpp"

namespace revisa::classify::rules {

namespace {

using catalog::Type;

// The edit texts without surrounding spaces, plus their lowercase and folded forms.
struct Words {
    std::string_view from, to;
    std::string from_lower, to_lower, from_fold, to_fold;
};

Words words(const Input& in) {
    Words w;
    w.from = trim(in.from);
    w.to = trim(in.to);
    w.from_lower = text::lower(w.from);
    w.to_lower = text::lower(w.to);
    w.from_fold = text::fold(w.from);
    w.to_fold = text::fold(w.to);
    return w;
}

// Non-empty and made only of letters (and combining marks).
bool is_word(std::string_view s) {
    if (s.empty()) return false;
    for (std::size_t i = 0; i < s.size();) {
        const text::Decoded d = text::decode(s, i);
        if (!text::is_letter(d.cp) && !text::is_mark(d.cp)) return false;
        i += d.len;
    }
    return true;
}

bool ends_with_any(std::string_view s, std::initializer_list<std::string_view> suffixes) {
    for (const std::string_view x : suffixes)
        if (s.ends_with(x)) return true;
    return false;
}

bool is_any(std::string_view s, std::initializer_list<std::string_view> words) {
    for (const std::string_view x : words)
        if (s == x) return true;
    return false;
}

bool has_written_accent(std::string_view lower) {
    for (const std::string_view v : {"á", "é", "í", "ó", "ú"})
        if (lower.find(v) != std::string_view::npos) return true;
    return false;
}

// Unstressed pronouns that can follow a verb (folded, no accents).
constexpr std::array<std::string_view, 11> kClitics = {"me", "te", "se", "nos", "os", "le",
                                                       "les", "lo", "la", "los", "las"};

// s is a sequence of up to three enclitic pronouns ("", "lo", "selo", "melo"…).
bool is_clitic_sequence(std::string_view s, int max_count = 3) {
    if (s.empty()) return true;
    if (max_count == 0) return false;
    for (const std::string_view c : kClitics)
        if (s.starts_with(c) && is_clitic_sequence(s.substr(c.size()), max_count - 1)) return true;
    return false;
}

// Folded verb forms that take an enclitic pronoun: an infinitive (llamar-le), a gerund
// (llamando-le), a «vosotros» imperative (llamad-le), a short imperative from a closed list
// (da-le, haz-le) or a form that keeps a written accent (llámale, dígale). Another enclitic may
// sit in between (tráeme-le). The accent test needs the original lowercase stems.
bool is_verb_stem(std::string_view fold, std::string_view from_stem, std::string_view to_stem, int depth = 0) {
    if (fold.size() < 2) return false;
    if (ends_with_any(fold, {"ar", "er", "ir", "ando", "iendo", "yendo"})) return true;
    if (fold.back() == 'd') return true;
    if (is_any(fold, {"da", "di", "haz", "pon", "ten"})) return true;
    if (has_written_accent(from_stem) || has_written_accent(to_stem)) return true;
    if (depth == 0)
        for (const std::string_view c : {"me", "te", "se", "nos", "os"})
            if (fold.size() > c.size() + 1 && fold.ends_with(c))
                return is_verb_stem(fold.substr(0, fold.size() - c.size()), {}, {}, 1);
    return false;
}

struct CliticPair {
    std::string_view de, a;
};

// le -> lo, la -> le…: the bare pronoun, or the same verb with that final enclitic.
bool clitic_swap(const Words& w, std::initializer_list<CliticPair> pairs) {
    for (const CliticPair& p : pairs) {
        if (w.from_lower == p.de && w.to_lower == p.a) return true;
        if (!is_word(w.from) || !is_word(w.to)) continue;
        if (!w.from_fold.ends_with(p.de) || !w.to_fold.ends_with(p.a)) continue;
        const std::string_view stem = std::string_view(w.from_fold).substr(0, w.from_fold.size() - p.de.size());
        if (stem != std::string_view(w.to_fold).substr(0, w.to_fold.size() - p.a.size())) continue;
        // The clitics are plain ASCII, so the lowercase stems end where the folded ones do.
        const std::string_view from_stem =
            std::string_view(w.from_lower).substr(0, w.from_lower.size() - p.de.size());
        const std::string_view to_stem = std::string_view(w.to_lower).substr(0, w.to_lower.size() - p.a.size());
        if (is_verb_stem(stem, from_stem, to_stem)) return true;
    }
    return false;
}

// Locutions where «de que» and «que» are both correct (DPD, s. v. «antes» and «después»): an
// added or removed «de» after them is neither dequeísmo nor queísmo.
bool optional_de(std::string_view prev_word) { return is_any(text::fold(prev_word), {"antes", "despues"}); }

// {expresión} of QUEISMO: the word before «que», with the word before it when that one is a
// pronoun, a preposition or an article joined to it by spaces only («me acuerdo», «a pesar»,
// «en caso», «el hecho»).
std::string_view queismo_expression(std::string_view left) {
    const std::string_view last = last_word(left);
    if (last.empty()) return last;
    const std::size_t start = static_cast<std::size_t>(last.data() - left.data());
    const std::string_view before = left.substr(0, start);
    const std::string_view prev = last_word(before);
    if (prev.empty()) return last;
    const std::size_t prev_end = static_cast<std::size_t>(prev.data() - left.data()) + prev.size();
    const std::string_view gap = left.substr(prev_end, start - prev_end);
    if (gap.empty() || !trim(gap).empty()) return last;
    if (!is_any(text::lower(prev), {"me", "te", "se", "nos", "os", "a", "al", "en", "el", "la", "los", "las",
                                    "un", "una"}))
        return last;
    return left.substr(prev_end - prev.size(), start + last.size() - (prev_end - prev.size()));
}

// Participles: regular -ado/-ido and the irregular ones (Nueva gramática, cap. 4).
bool is_participle(std::string_view fold) {
    if (ends_with_any(fold, {"ado", "ido"})) return true;
    return is_any(fold, {"abierto", "cubierto", "descubierto", "dicho", "escrito", "hecho", "deshecho", "satisfecho",
                         "muerto", "puesto", "compuesto", "dispuesto", "expuesto", "impuesto", "propuesto",
                         "supuesto", "resuelto", "roto", "visto", "previsto", "vuelto", "devuelto", "envuelto",
                         "frito", "impreso", "provisto"});
}

std::size_t length(std::string_view s) { return text::code_points(s); }

}  // namespace

// 21. «creo de que» -> «creo que»: only «de» is removed, right before «que».
std::optional<Result> dequeismo(const Input& in) {
    const Words w = words(in);
    if (w.from_lower != "de" || !w.to.empty()) return std::nullopt;
    if (text::fold(in.next_word) != "que" || in.prev_word.empty() || optional_de(in.prev_word)) return std::nullopt;
    return make(Type::Dequeismo, {}, {{"verbo", in.prev_word}});
}

// 22. «me acuerdo que» -> «me acuerdo de que»: only «de» is added, right before «que».
std::optional<Result> queismo(const Input& in) {
    const Words w = words(in);
    if (!w.from.empty() || w.to_lower != "de") return std::nullopt;
    if (text::fold(in.next_word) != "que" || in.prev_word.empty() || optional_de(in.prev_word)) return std::nullopt;
    return make(Type::Queismo, {}, {{"expresión", queismo_expression(in.ctx.left)}});
}

// 23-25. Unstressed pronouns, bare or enclitic, keeping the number (le -> lo/la, les -> los/las).
std::optional<Result> leismo(const Input& in) {
    const Words w = words(in);
    if (!clitic_swap(w, {{"le", "lo"}, {"le", "la"}, {"les", "los"}, {"les", "las"}})) return std::nullopt;
    return make(Type::Leismo, {}, {{"de", w.from}, {"a", w.to}});
}

std::optional<Result> laismo(const Input& in) {
    const Words w = words(in);
    if (!clitic_swap(w, {{"la", "le"}, {"las", "les"}})) return std::nullopt;
    return make(Type::Laismo, {}, {{"de", w.from}, {"a", w.to}});
}

std::optional<Result> loismo(const Input& in) {
    const Words w = words(in);
    if (!clitic_swap(w, {{"lo", "le"}, {"los", "les"}})) return std::nullopt;
    return make(Type::Loismo, {}, {{"de", w.from}, {"a", w.to}});
}

// 26. A plural form of «haber» -> its singular (haber_plural.tsv). Not when the next word is a
// participle other than «habido»: then «haber» is the auxiliary of a compound tense.
std::optional<Result> haber_impersonal(const Input& in) {
    const Words w = words(in);
    const std::string next = text::fold(in.next_word);
    const bool habido = next == "habido";
    for (const catalog::Row& r : catalog::rows("haber_plural.tsv")) {
        if (r.cols.size() < 2 || r.cols[0] != w.from_lower || r.cols[1] != w.to_lower) continue;
        const bool needs_habido = r.cols.size() > 2 && r.cols[2] == "habido";
        if (needs_habido ? !habido : (!habido && is_participle(next))) return std::nullopt;
        if (!habido) return make(Type::HaberImpersonal, {}, {{"de", w.from}, {"a", w.to}});
        const std::string a = std::string(w.to) + " " + std::string(in.next_word);
        return make(Type::HaberImpersonal, {}, {{"de", w.from}, {"a", a}});
    }
    return std::nullopt;
}

// 27. Infinitive -> «vosotros» imperative: callar -> callad, decirle -> decidle (r -> d before the
// enclitics), callaros -> callaos (the r drops before «os»). «iros»/«idos» is an admitted variant.
std::optional<Result> imperativo(const Input& in) {
    const Words w = words(in);
    if (!is_word(w.from) || !is_word(w.to)) return std::nullopt;
    const std::string_view f = w.from_fold, t = w.to_fold;
    std::size_t k = 0;
    while (k < f.size() && k < t.size() && f[k] == t[k]) ++k;
    if (k == 0 || k == f.size() || f[k] != 'r' || !is_any(f.substr(k - 1, 1), {"a", "e", "i"})) return std::nullopt;
    bool match = false;
    if (f.size() == t.size() + 1) {
        // callaros -> callaos, comérosla -> coméosla
        match = f.substr(k + 1) == t.substr(k) && t.substr(k).starts_with("os") && is_clitic_sequence(t.substr(k + 2), 2);
    } else if (f.size() == t.size()) {
        // venir -> venid, decírselo -> decídselo (but not callaros -> callados)
        const std::string_view rest = f.substr(k + 1);
        match = t[k] == 'd' && rest == t.substr(k + 1) && !rest.starts_with("os") && is_clitic_sequence(rest);
    }
    if (!match) return std::nullopt;
    return make(Type::Imperativo, {}, {{"de", w.from}, {"a", w.to}});
}

// 28. dijistes -> dijiste: «de» is «a» plus a final «s», and «a» ends in «-ste». Only the endings
// of that preterite (-aste, -iste), and not the words that end like it: the present of -sistir
// and -xistir (existes -> existe) and a few nouns and adjectives (tristes -> triste).
std::optional<Result> preterito_s(const Input& in) {
    const Words w = words(in);
    if (!is_word(w.from) || w.from_lower != w.to_lower + "s") return std::nullopt;
    const std::string_view t = w.to_fold;
    if (!ends_with_any(t, {"aste", "iste"}) || ends_with_any(t, {"siste", "xiste"})) return std::nullopt;
    if (is_any(t, {"triste", "chiste", "quiste", "alpiste", "contraste", "traste", "desgaste"})) return std::nullopt;
    return make(Type::PreteritoS, {}, {{"de", w.from}, {"a", w.to}});
}

// 29. A wrong verb form of the closed list (conjugacion.tsv) -> the form the list gives.
std::optional<Result> conjugacion(const Input& in) {
    const Words w = words(in);
    for (const catalog::Row& r : catalog::rows("conjugacion.tsv")) {
        if (r.cols.size() < 2 || r.cols[0] != w.from_lower || r.cols[1] != w.to_lower) continue;
        if (r.cols.size() > 2 && !r.cols[2].empty())
            return Result{Type::Conjugacion, catalog::fill(r.cols[2], {{"de", w.from}, {"a", w.to}})};
        return make(Type::Conjugacion, {}, {{"de", w.from}, {"a", w.to}});
    }
    return std::nullopt;
}

namespace {

// Determiners whose forms do not share a stem (el/la) or are too short for the stem rule.
constexpr std::array<std::array<std::string_view, 4>, 8> kDeterminers = {{
    {"el", "la", "los", "las"},
    {"un", "una", "unos", "unas"},
    {"este", "esta", "estos", "estas"},
    {"ese", "esa", "esos", "esas"},
    {"aquel", "aquella", "aquellos", "aquellas"},
    {"mi", "mis", "", ""},
    {"tu", "tus", "", ""},
    {"su", "sus", "", ""},
}};

bool same_determiner(std::string_view a, std::string_view b) {
    for (const auto& g : kDeterminers) {
        bool has_a = false, has_b = false;
        for (const std::string_view x : g) {
            if (x.empty()) continue;
            has_a = has_a || x == a;
            has_b = has_b || x == b;
        }
        if (has_a && has_b) return true;
    }
    return false;
}

enum class Stem { Any, Vowel, Consonant };

struct Ending {
    std::string_view x, y;
    Stem stem;
};

// Pairs of endings after the common stem (folded). Number: ∅/-s (casa, vende), ∅/-es (mes,
// canción), -z/-ces (vez); number of the verb: ∅/-n (vende, venden). Gender: -o/-a and its
// plurals; ∅/-a after -r, -n, -s, -l, -z (profesor, español, inglés, andaluz, algún).
constexpr std::array<Ending, 13> kEndings = {{
    {"", "s", Stem::Vowel},
    {"", "es", Stem::Any},
    {"", "n", Stem::Vowel},
    {"z", "ces", Stem::Any},
    {"o", "a", Stem::Any},
    {"os", "as", Stem::Any},
    {"o", "as", Stem::Any},
    {"a", "os", Stem::Any},
    {"", "a", Stem::Consonant},
    {"", "as", Stem::Consonant},
    {"", "os", Stem::Consonant},
    {"es", "a", Stem::Consonant},
    {"es", "as", Stem::Consonant},
}};

bool stem_ok(std::string_view stem, Stem kind) {
    if (stem.empty()) return false;
    const char last = stem.back();
    switch (kind) {
        case Stem::Any: return true;
        case Stem::Vowel: return is_any(stem.substr(stem.size() - 1), {"a", "e", "i", "o", "u"});
        case Stem::Consonant: return last == 'r' || last == 'n' || last == 's' || last == 'l' || last == 'z';
    }
    return false;
}

}  // namespace

// 30. Same stem, only the gender or number ending changes (bonitos -> bonitas, la -> el).
std::optional<Result> concordancia(const Input& in) {
    const Words w = words(in);
    if (!is_word(w.from) || !is_word(w.to) || w.from_fold == w.to_fold) return std::nullopt;
    bool match = same_determiner(w.from_lower, w.to_lower);
    if (!match) {
        const std::string_view f = w.from_fold, t = w.to_fold;
        if (std::min(length(f), length(t)) < 3) return std::nullopt;
        std::size_t k = 0;
        while (k < f.size() && k < t.size() && f[k] == t[k]) ++k;
        const std::string_view stem = f.substr(0, k), sf = f.substr(k), st = t.substr(k);
        for (const Ending& e : kEndings)
            if (((sf == e.x && st == e.y) || (sf == e.y && st == e.x)) && stem_ok(stem, e.stem)) match = true;
        // A misspelt word turned into a correct one is ERRATA (tambie -> también), not this.
        if (match && catalog::in_dictionary(w.to) && !catalog::in_dictionary(w.from)) return std::nullopt;
    }
    if (!match) return std::nullopt;
    return make(Type::Concordancia, {}, {{"de", w.from}, {"a", w.to}});
}

// 31. Only one comma is removed (spaces around it may go with it).
std::optional<Result> coma_sobra(const Input& in) {
    if (trim(in.from) != "," || !trim(in.to).empty()) return std::nullopt;
    return make(Type::ComaSobra);
}

}  // namespace revisa::classify::rules
