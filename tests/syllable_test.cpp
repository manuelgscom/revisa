// core/classify/syllable: orthographic syllabification and stress (tests/syllable/*.jsonl, 100 %).
#include "doctest.h"

#include <string>

#include "core/classify/syllable.hpp"
#include "jsonl.hpp"

using namespace revisa;

TEST_CASE("syllable: the cases of tests/syllable/*.jsonl") {
    const auto cases = test::load_jsonl_dir("syllable");
    CHECK(cases.size() >= 150);
    for (const auto& c : cases) {
        const std::string word = c.j.at("word");
        const auto s = classify::syllabify(word);
        REQUIRE_MESSAGE(s.has_value(), c.where << ": " << word);
        CHECK_MESSAGE(classify::join_syllables(*s) == c.j.at("syllables").get<std::string>(),
                      c.where << ": " << word << " -> " << classify::join_syllables(*s));
        CHECK_MESSAGE(classify::stress_name(s->stress) == c.j.at("stress").get<std::string>(),
                      c.where << ": " << word << " -> " << classify::stress_name(s->stress));
        if (c.j.contains("hiatus"))
            CHECK_MESSAGE(s->hiatus_tilde == c.j.at("hiatus").get<bool>(), c.where << ": " << word);
    }
}

TEST_CASE("syllable: endings as the accent rules see them") {
    using classify::Ending;
    const auto ending = [](const char* w) { return classify::syllabify(w)->ending; };
    CHECK(ending("casa") == Ending::Vowel);
    CHECK(ending("camión") == Ending::N);
    CHECK(ending("compás") == Ending::S);
    CHECK(ending("virrey") == Ending::Y);
    CHECK(ending("reloj") == Ending::Consonant);
    CHECK(ending("bíceps") == Ending::Cluster);
    CHECK(ending("robots") == Ending::Cluster);
    CHECK(ending("récord") == Ending::Cluster);
    CHECK(classify::syllabify("guion")->vowels == 2);
    CHECK(classify::syllabify("ti")->vowels == 1);
    CHECK(classify::syllabify("árbol")->tilde);
    CHECK_FALSE(classify::syllabify("arbol")->tilde);
}

TEST_CASE("syllable: anything but Spanish letters has no syllables") {
    CHECK_FALSE(classify::syllabify("").has_value());
    CHECK_FALSE(classify::syllabify("123").has_value());
    CHECK_FALSE(classify::syllabify("e-mail").has_value());
    CHECK_FALSE(classify::syllabify("garçon").has_value());
    CHECK_FALSE(classify::syllabify("dos palabras").has_value());
    CHECK_FALSE(classify::syllabify("bcd").has_value());
}
