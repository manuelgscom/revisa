// core/classify: the spelling rules, types 1-20 (tests/classify/<TIPO>.jsonl, 100 %).
// Each file has positive cases and negatives (similar pairs that must get another type or OTRO).
#include "doctest.h"

#include <filesystem>
#include <set>
#include <string>

#include "core/catalog/dictionary.hpp"
#include "core/classify/classify.hpp"
#include "jsonl.hpp"

using namespace revisa;

namespace {

constexpr const char* kSpellingTypes[] = {
    "ESPACIADO",   "MAYUSCULA",     "TILDE_DIACRITICA", "TILDE_INTERROGATIVA", "POR_QUE",
    "TILDE_FALTA", "TILDE_SOBRA",   "HABER_A_VER",      "VERBO_HABER_H",       "ECHO_HECHO",
    "HAY_AHI_AY",  "SINO_SI_NO",    "JUNTO_SEPARADO",   "B_V",                 "G_J",
    "H",           "LL_Y",          "C_Z_S_X",          "SIGNO_APERTURA",      "ERRATA",
};

void run_file(const std::string& type) {
    const auto cases = test::load_jsonl(std::filesystem::path(REVISA_TESTS_DIR) / "classify" / (type + ".jsonl"));
    CHECK_MESSAGE(cases.size() >= 15, type);
    int positives = 0, negatives = 0;
    for (const auto& c : cases) {
        const std::string from = c.j.at("from"), to = c.j.at("to");
        const std::string left = c.j.value("left", ""), right = c.j.value("right", "");
        const std::string expected = c.j.at("type");
        REQUIRE_MESSAGE(catalog::type_from_id(expected).has_value(), c.where);
        (expected == type ? positives : negatives)++;
        const classify::Result r = classify::classify(from, to, {left, right});
        const std::string got(catalog::type_id(r.type));
        CHECK_MESSAGE(got == expected, c.where << ": «" << from << "» -> «" << to << "» gave " << got);
        if (c.j.contains("explanation")) {
            const std::string want = c.j.at("explanation");
            CHECK_MESSAGE(r.explanation.value_or("<none>") == want,
                          c.where << ": explanation «" << r.explanation.value_or("<none>") << "»");
        }
        CHECK_MESSAGE(r.explanation.has_value() == (r.type != catalog::Type::Otro), c.where);
        // No template marker ({a}, {de}, {regla}…) may reach the user unfilled.
        CHECK_MESSAGE(r.explanation.value_or("").find('{') == std::string::npos,
                      c.where << ": unfilled marker in «" << r.explanation.value_or("") << "»");
    }
    CHECK_MESSAGE(positives > 0, type << ": no positive case");
    CHECK_MESSAGE(negatives > 0, type << ": no negative case");
}

}  // namespace

TEST_CASE("classify_orto: the cases of types 1-19") {
    for (const char* type : kSpellingTypes) {
        if (std::string(type) == "ERRATA") continue;
        run_file(type);
    }
}

TEST_CASE("classify_orto: the cases of ERRATA") {
    if (!catalog::in_dictionary("casa")) {
        MESSAGE("ERRATA skipped: the dictionary is still the stub");
        return;
    }
    run_file("ERRATA");
}

TEST_CASE("classify_orto: identical texts and OTRO") {
    CHECK(classify::classify("casa", "casa").type == catalog::Type::Otro);
    CHECK_FALSE(classify::classify("casa", "casa").explanation.has_value());
    CHECK(classify::classify("perro", "gato").type == catalog::Type::Otro);
}
