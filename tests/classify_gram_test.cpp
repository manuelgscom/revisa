// core/classify, grammar types 21-31: tests/classify/<TYPE>.jsonl at 100 %, plus
// checks of their closed lists (haber_plural.tsv, conjugacion.tsv).
#include "doctest.h"

#include <set>
#include <string>

#include "core/catalog/catalog.hpp"
#include "core/classify/classify.hpp"
#include "jsonl.hpp"

using namespace revisa;

namespace {

constexpr std::string_view kGrammarTypes[] = {
    "DEQUEISMO",        "QUEISMO",    "LEISMO",      "LAISMO",      "LOISMO",       "HABER_IMPERSONAL",
    "IMPERATIVO",       "PRETERITO_S", "CONJUGACION", "CONCORDANCIA", "COMA_SOBRA",
};

void run_file(std::string_view type) {
    const auto path = std::filesystem::path(REVISA_TESTS_DIR) / "classify" / (std::string(type) + ".jsonl");
    const auto cases = test::load_jsonl(path);
    int positives = 0, negatives = 0;
    for (const auto& c : cases) {
        const std::string from = c.j.at("from"), to = c.j.at("to");
        const std::string left = c.j.value("left", ""), right = c.j.value("right", "");
        const std::string expected = c.j.at("type");
        REQUIRE_MESSAGE(catalog::type_from_id(expected).has_value(), c.where << ": unknown type " << expected);
        const classify::Result r = classify::classify(from, to, {left, right});
        const std::string_view got = catalog::type_id(r.type);
        CHECK_MESSAGE(got == expected, c.where << ": «" << from << "» -> «" << to << "»: got " << got);
        if (c.j.contains("explanation")) {
            CHECK_MESSAGE(r.explanation.value_or("") == c.j.at("explanation").get<std::string>(),
                          c.where << ": explanation «" << r.explanation.value_or("") << "»");
        }
        CHECK_MESSAGE(r.explanation.has_value() == (r.type != catalog::Type::Otro), c.where);
        // No template marker ({a}, {de}, {regla}…) may reach the user unfilled.
        CHECK_MESSAGE(r.explanation.value_or("").find('{') == std::string::npos,
                      c.where << ": unfilled marker in «" << r.explanation.value_or("") << "»");
        (expected == type ? positives : negatives)++;
    }
    CHECK_MESSAGE(positives + negatives >= 15, type << ": fewer than 15 cases");
    CHECK_MESSAGE(positives >= 8, type << ": fewer than 8 positive cases");
    CHECK_MESSAGE(negatives >= 5, type << ": fewer than 5 negative cases");
}

}  // namespace

TEST_CASE("classify_gram: the cases of every grammar type") {
    for (const std::string_view type : kGrammarTypes) {
        CAPTURE(type);
        run_file(type);
    }
}

TEST_CASE("classify_gram: haber_plural.tsv rows classify as HABER_IMPERSONAL") {
    std::set<std::string_view> seen;
    for (const catalog::Row& r : catalog::rows("haber_plural.tsv")) {
        CAPTURE(r.line);
        REQUIRE((r.cols.size() == 2 || r.cols.size() == 3));
        CHECK(seen.insert(r.cols[0]).second);
        if (r.cols.size() == 3) CHECK(r.cols[2] == "habido");
        const std::string right = r.cols.size() == 3 ? " habido quejas." : " muchas personas.";
        const classify::Result res = classify::classify(r.cols[0], r.cols[1], {"", right});
        CHECK(res.type == catalog::Type::HaberImpersonal);
    }
    CHECK(seen.size() >= 8);
}

TEST_CASE("classify_gram: conjugacion.tsv is well formed and every row classifies as CONJUGACION") {
    std::set<std::pair<std::string_view, std::string_view>> seen;
    for (const catalog::Row& r : catalog::rows("conjugacion.tsv")) {
        CAPTURE(r.line);
        REQUIRE((r.cols.size() == 2 || r.cols.size() == 3));
        CHECK(!r.cols[0].empty());
        CHECK(!r.cols[1].empty());
        CHECK(r.cols[0] != r.cols[1]);
        CHECK(seen.emplace(r.cols[0], r.cols[1]).second);
        if (r.cols.size() == 3) CHECK(r.cols[2].find('"') == std::string_view::npos);
        const classify::Result res = classify::classify(r.cols[0], r.cols[1], {"Ayer ", "."});
        CHECK(res.type == catalog::Type::Conjugacion);
    }
    CHECK(seen.size() >= 40);
    CHECK(seen.size() <= 80);
}
