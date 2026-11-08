// catalog::admitted_variant: variantes_admitidas.tsv.
#include "doctest.h"

#include <set>
#include <string>

#include "core/catalog/catalog.hpp"
#include "core/text/text.hpp"

using namespace revisa;

TEST_CASE("variants: the admitted pairs in both directions, ignoring case and spaces") {
    CHECK(catalog::admitted_variant("quizá", "quizás"));
    CHECK(catalog::admitted_variant("quizás", "quizá"));
    CHECK(catalog::admitted_variant("Quizás", "quizá"));
    CHECK(catalog::admitted_variant("en seguida", "enseguida"));
    CHECK(catalog::admitted_variant(" enseguida ", "en seguida"));
    CHECK(catalog::admitted_variant("iros", "idos"));
    CHECK(catalog::admitted_variant("idos", "iros"));
    CHECK(catalog::admitted_variant("solo", "sólo"));
    CHECK(catalog::admitted_variant("Sólo", "Solo"));
    CHECK(catalog::admitted_variant("éste", "este"));
    CHECK(catalog::admitted_variant("esas", "ésas"));
    CHECK(catalog::admitted_variant("aquéllas", "aquellas"));
    CHECK(catalog::admitted_variant("ÉSTE", "este"));
}

TEST_CASE("variants: double accentuation, with the same plural or feminine on both sides") {
    CHECK(catalog::admitted_variant("vídeo", "video"));
    CHECK(catalog::admitted_variant("periodo", "período"));
    CHECK(catalog::admitted_variant("vídeos", "videos"));
    CHECK(catalog::admitted_variant("chóferes", "choferes"));
    CHECK(catalog::admitted_variant("cócteles", "cocteles"));
    CHECK(catalog::admitted_variant("élites", "elites"));
    CHECK(catalog::admitted_variant("cardíaca", "cardiaca"));
    CHECK(catalog::admitted_variant("policiacas", "policíacas"));
    CHECK(catalog::admitted_variant("setiembre", "septiembre"));
}

TEST_CASE("variants: pairs that are not admitted variants") {
    CHECK_FALSE(catalog::admitted_variant("esto", "ésto"));   // neuter: never with a tilde
    CHECK_FALSE(catalog::admitted_variant("está", "esta"));   // verb
    CHECK_FALSE(catalog::admitted_variant("guión", "guion"));  // only «guion» since 2010
    CHECK_FALSE(catalog::admitted_variant("mas", "más"));
    CHECK_FALSE(catalog::admitted_variant("tu", "tú"));
    CHECK_FALSE(catalog::admitted_variant("aun", "aún"));
    CHECK_FALSE(catalog::admitted_variant("solo", "sola"));
    CHECK_FALSE(catalog::admitted_variant("quizá", "quizá"));
    CHECK_FALSE(catalog::admitted_variant("video", "vídeos"));
    CHECK_FALSE(catalog::admitted_variant("callaros", "callaos"));
    CHECK_FALSE(catalog::admitted_variant("", ""));
    CHECK_FALSE(catalog::admitted_variant("s", "es"));
}

TEST_CASE("variants: every row is two different forms, admitted both ways, and accent rows differ only in accents") {
    // Rows that are not a pure accent variant.
    const std::set<std::string_view> spelling = {"en seguida", "quizá", "iros", "septiembre"};
    std::set<std::string_view> seen;
    for (const catalog::Row& r : catalog::rows("variantes_admitidas.tsv")) {
        CAPTURE(r.line);
        REQUIRE(r.cols.size() == 2);
        CHECK(r.cols[0] != r.cols[1]);
        CHECK(text::lower(r.cols[0]) == r.cols[0]);
        CHECK(text::lower(r.cols[1]) == r.cols[1]);
        CHECK(seen.insert(r.cols[0]).second);
        CHECK(seen.insert(r.cols[1]).second);
        CHECK(catalog::admitted_variant(r.cols[0], r.cols[1]));
        CHECK(catalog::admitted_variant(r.cols[1], r.cols[0]));
        if (!spelling.contains(r.cols[0])) CHECK(text::fold(r.cols[0]) == text::fold(r.cols[1]));
    }
}
