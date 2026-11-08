// core/segment: sentence cuts, trimming and long segments (tests/segment/*.jsonl, 100 %).
#include "doctest.h"

#include <string>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/infer/prompt.hpp"
#include "core/segment/segment.hpp"
#include "core/text/text.hpp"
#include "jsonl.hpp"

using namespace revisa;

namespace {

// Only whitespace outside the segments; segments sorted, non-empty, trimmed, at code point
// boundaries; too_long exactly when longer than kMaxSegmentChars.
void check_invariants(const std::string& where, std::string_view t, const std::vector<segment::Segment>& segs) {
    std::size_t prev = 0;
    for (const segment::Segment& s : segs) {
        REQUIRE_MESSAGE(s.start < s.end, where);
        REQUIRE_MESSAGE(s.end <= t.size(), where);
        REQUIRE_MESSAGE(prev <= s.start, where);
        for (std::size_t i = prev; i < s.start;) {
            const text::Decoded d = text::decode(t, i);
            CHECK_MESSAGE(text::is_space(d.cp), where << ": non-space outside the segments at byte " << i);
            i += d.len;
        }
        const std::string_view body = t.substr(s.start, s.end - s.start);
        CHECK_MESSAGE(infer::trim_like_python(body) == body, where << ": untrimmed segment");
        CHECK_MESSAGE(s.too_long == (text::code_points(body) > infer::kMaxSegmentChars), where);
        prev = s.end;
    }
    for (std::size_t i = prev; i < t.size();) {
        const text::Decoded d = text::decode(t, i);
        CHECK_MESSAGE(text::is_space(d.cp), where << ": non-space after the last segment");
        i += d.len;
    }
}

}  // namespace

TEST_CASE("segment: jsonl cases") {
    const auto cases = test::load_jsonl_dir("segment");
    REQUIRE(cases.size() >= 40);
    for (const auto& c : cases) {
        const std::string t = c.j.at("text").get<std::string>();
        const auto segs = segment::segment(t);
        std::vector<std::string> got;
        std::vector<bool> got_long;
        for (const auto& s : segs) {
            got.push_back(t.substr(s.start, s.end - s.start));
            got_long.push_back(s.too_long);
        }
        const auto want = c.j.at("segments").get<std::vector<std::string>>();
        CHECK_MESSAGE(got == want, c.where << ": got " << nlohmann::json(got).dump());
        std::vector<bool> want_long(want.size(), false);
        if (c.j.contains("too_long")) want_long = c.j.at("too_long").get<std::vector<bool>>();
        CHECK_MESSAGE(got_long == want_long, c.where);
        check_invariants(c.where, t, segs);
    }
}

TEST_CASE("segment: blank text gives no segments") {
    CHECK(segment::segment("").empty());
    CHECK(segment::segment(" \n\t\r\n  ").empty());
}

TEST_CASE("segment: abreviaturas.txt is a closed list of dotted abbreviations") {
    const auto& rows = catalog::rows("abreviaturas.txt");
    CHECK(rows.size() >= 50);
    for (const catalog::Row& r : rows) {
        REQUIRE(!r.cols.empty());
        CHECK_MESSAGE(r.cols[0].find('.') != std::string_view::npos, r.cols[0]);
        CHECK_MESSAGE(r.cols.size() <= 2, r.cols[0]);
        if (r.cols.size() == 2) CHECK_MESSAGE(r.cols[1] == "fin", r.cols[0]);
    }
}

TEST_CASE("segment: long segments") {
    // 900 code points with '; ' every ~100: split into parts of at most 400.
    std::string sentence;
    for (int i = 0; i < 9; ++i) sentence += std::string(97, 'a') + "ñ; ";
    sentence += "fin.";
    const auto segs = segment::segment(sentence);
    REQUIRE(segs.size() == 3);
    for (const auto& s : segs) {
        CHECK_FALSE(s.too_long);
        CHECK(text::code_points(std::string_view(sentence).substr(s.start, s.end - s.start)) <= 400);
    }
    check_invariants("long with ';'", sentence, segs);

    // 900 code points without ';' or ':': one segment, too long.
    const std::string flat(900, 'a');
    const auto one = segment::segment(flat);
    REQUIRE(one.size() == 1);
    CHECK(one[0].too_long);

    // Exactly 400 code points fit (with multibyte characters).
    std::string limit;
    for (int i = 0; i < 400; ++i) limit += "é";
    const auto fit = segment::segment("  " + limit + "  ");
    REQUIRE(fit.size() == 1);
    CHECK_FALSE(fit[0].too_long);
    CHECK(segment::segment(limit + "é")[0].too_long);
}
