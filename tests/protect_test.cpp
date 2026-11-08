// core/protect: protected spans, fully protected segments and special tokens
// (tests/protect/*.jsonl, 100 %).
#include "doctest.h"

#include <string>
#include <vector>

#include "core/protect/protect.hpp"
#include "jsonl.hpp"

using namespace revisa;

TEST_CASE("protect: jsonl cases") {
    const auto cases = test::load_jsonl_dir("protect");
    REQUIRE(cases.size() >= 40);
    for (const auto& c : cases) {
        const std::string s = c.j.at("segment").get<std::string>();
        const auto spans = protect::protect(s);
        std::vector<std::string> got;
        std::size_t prev_end = 0;
        for (std::size_t i = 0; i < spans.size(); ++i) {
            REQUIRE_MESSAGE(spans[i].start < spans[i].end, c.where);
            REQUIRE_MESSAGE(spans[i].end <= s.size(), c.where);
            if (i > 0) CHECK_MESSAGE(spans[i].start > prev_end, c.where << ": spans not merged");
            prev_end = spans[i].end;
            got.push_back(s.substr(spans[i].start, spans[i].end - spans[i].start));
        }
        const auto want = c.j.at("spans").get<std::vector<std::string>>();
        CHECK_MESSAGE(got == want, c.where << ": got " << nlohmann::json(got).dump());
        CHECK_MESSAGE(protect::fully_protected(s, spans) == c.j.value("fully_protected", false), c.where);
        CHECK_MESSAGE(protect::has_special_token(s) == c.j.value("special", false), c.where);
    }
}

TEST_CASE("protect: fully_protected with hand-made spans") {
    CHECK(protect::fully_protected("", {}));
    CHECK(protect::fully_protected("¡¿...?!", {}));
    CHECK_FALSE(protect::fully_protected("hola", {}));
    CHECK(protect::fully_protected("hola", {{0, 4}}));
    CHECK_FALSE(protect::fully_protected("hola 1", {{0, 4}}));
    CHECK(protect::fully_protected("(hola) — 😀", {{1, 5}}));
}
