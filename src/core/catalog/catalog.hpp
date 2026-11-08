#pragma once
// Error catalog v1: type ids, closed lists and fixed explanations.
//
// The data lives in src/core/catalog/ as UTF-8 text files and is embedded in the binary at build
// time (cmake/embed.cmake). The dataset tooling (Python) reads the same files.
// Format of every data file:
//   - one row per line, fields separated by a TAB;
//   - blank lines and lines starting with '#' are skipped (comments cite the normative source:
//     Ortografía 2010, DPD, Nueva gramática);
//   - no quoting, no escaping; a trailing "\r" is removed.
//
// Files:
//   tipos.tsv                   id <TAB> inyectable           the 31 types, in catalog order
//   explicaciones/*.tsv         id <TAB> variante <TAB> plantilla
//                               (variante "" = the default one; placeholders {de}, {a}, {letra},
//                                {regla}, {verbo}, {expresión}…)
//   pares/<ID>.tsv              de <TAB> a <TAB> explicación  pair lists of one type
//   other lists                 one file per list, described in its header comment
//   hunspell/es_ES.{aff,dic}    LibreOffice's RLA-ES dictionary (ERRATA), see dictionary.hpp

#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace revisa::catalog {

// Version of the catalog. Adding a type or changing a rule bumps it; ids are never renamed.
inline constexpr int kCatalogVersion = 1;

// The types, in the order of the catalog tables, then OTRO.
enum class Type : unsigned char {
    Espaciado,
    Mayuscula,
    TildeDiacritica,
    TildeInterrogativa,
    PorQue,
    TildeFalta,
    TildeSobra,
    HaberAVer,
    VerboHaberH,
    EchoHecho,
    HayAhiAy,
    SinoSiNo,
    JuntoSeparado,
    BV,
    GJ,
    H,
    LlY,
    CZSX,
    SignoApertura,
    Errata,
    Dequeismo,
    Queismo,
    Leismo,
    Laismo,
    Loismo,
    HaberImpersonal,
    Imperativo,
    PreteritoS,
    Conjugacion,
    Concordancia,
    ComaSobra,
    Otro,
};

inline constexpr std::size_t kTypeCount = 32;  // including OTRO

inline constexpr std::array<std::string_view, kTypeCount> kTypeIds = {
    "ESPACIADO",     "MAYUSCULA",        "TILDE_DIACRITICA", "TILDE_INTERROGATIVA", "POR_QUE",
    "TILDE_FALTA",   "TILDE_SOBRA",      "HABER_A_VER",      "VERBO_HABER_H",       "ECHO_HECHO",
    "HAY_AHI_AY",    "SINO_SI_NO",       "JUNTO_SEPARADO",   "B_V",                 "G_J",
    "H",             "LL_Y",             "C_Z_S_X",          "SIGNO_APERTURA",      "ERRATA",
    "DEQUEISMO",     "QUEISMO",          "LEISMO",           "LAISMO",              "LOISMO",
    "HABER_IMPERSONAL", "IMPERATIVO",    "PRETERITO_S",      "CONJUGACION",         "CONCORDANCIA",
    "COMA_SOBRA",    "OTRO",
};

constexpr std::string_view type_id(Type t) { return kTypeIds[static_cast<std::size_t>(t)]; }
std::optional<Type> type_from_id(std::string_view id);

// --- Data files -------------------------------------------------------------------------------

struct Row {
    std::vector<std::string_view> cols;  // views into the embedded file
    std::size_t line = 0;                // 1-based line number in the file
};

// Raw bytes of an embedded data file, by its path relative to src/core/catalog/
// ("tipos.tsv", "pares/POR_QUE.tsv", "hunspell/es_ES.aff"). nullopt if it is not embedded.
std::optional<std::string_view> data_file(std::string_view path);

// Paths of every embedded data file, sorted.
std::vector<std::string_view> data_files();

// Parsed rows of a data file (cached; safe to call from several threads). A missing file is a
// build error: it aborts with a message on stderr.
const std::vector<Row>& rows(std::string_view path);

// --- Explanations -----------------------------------------------------------------------------

// The template of (type, variant) from explicaciones/*.tsv; variant "" is the default one.
// nullopt if there is no such row.
std::optional<std::string_view> explanation_template(Type type, std::string_view variant = {});

// Replaces each "{name}" of tmpl with its value. Unknown placeholders are left as they are
// (catalog_test checks that every template only uses known names).
std::string fill(std::string_view tmpl,
                 std::initializer_list<std::pair<std::string_view, std::string_view>> vars = {});

// --- Admitted variants ------------------------------------------------------------------------

// true if from -> to (in either direction, ignoring case) only swaps two forms that the norm
// admits ("quizá"/"quizás", "solo"/"sólo", "este"/"éste"…): the guards discard such an edit.
// Data: variantes_admitidas.tsv. Implemented in variants.cpp.
bool admitted_variant(std::string_view from, std::string_view to);

}  // namespace revisa::catalog
