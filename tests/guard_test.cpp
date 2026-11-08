// Guards (src/core/guard) over hand-written edits: tests/guard/*.jsonl must pass 100 %.
#include "doctest.h"

#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/guard/config.hpp"
#include "core/guard/guard.hpp"
#include "jsonl.hpp"

using revisa::guard::Item;
using revisa::guard::Rejection;
namespace diff = revisa::diff;

namespace {

// A segment written with its edits inline as [from|to].
struct Marked {
    std::string segment, corrected;
    std::vector<diff::Edit> edits;
};

Marked parse_marked(const std::string& m) {
    Marked out;
    for (std::size_t i = 0; i < m.size();) {
        if (m[i] != '[') {
            out.segment += m[i];
            out.corrected += m[i];
            ++i;
            continue;
        }
        const std::size_t bar = m.find('|', i);
        const std::size_t close = m.find(']', bar);
        REQUIRE(bar != std::string::npos);
        REQUIRE(close != std::string::npos);
        diff::Edit e;
        e.from = m.substr(i + 1, bar - i - 1);
        e.to = m.substr(bar + 1, close - bar - 1);
        e.start = out.segment.size();
        e.end = e.start + e.from.size();
        e.to_start = out.corrected.size();
        e.to_end = e.to_start + e.to.size();
        out.segment += e.from;
        out.corrected += e.to;
        out.edits.push_back(e);
        i = close + 1;
    }
    return out;
}

const char* rejection_name(Rejection r) {
    switch (r) {
        case Rejection::None: return "None";
        case Rejection::InvalidOutput: return "InvalidOutput";
        case Rejection::ChangeRatio: return "ChangeRatio";
        case Rejection::TooManyOtro: return "TooManyOtro";
    }
    return "?";
}

revisa::catalog::Type type_of(const nlohmann::json& id) {
    const auto t = revisa::catalog::type_from_id(id.get<std::string>());
    REQUIRE_MESSAGE(t.has_value(), id.get<std::string>());
    return *t;
}

Item item(diff::Edit e, revisa::catalog::Type t) {
    Item it{std::move(e), {}};
    it.result.type = t;
    if (t != revisa::catalog::Type::Otro) it.result.explanation = "x";
    return it;
}

}  // namespace

// Thresholds the case files under tests/guard/ were written for.
const revisa::guard::Thresholds kCaseThresholds{0.3, 1};

TEST_CASE("guard: tests/guard/*.jsonl") {
    const auto cases = revisa::test::load_jsonl_dir("guard");
    CHECK(cases.size() >= 60);
    for (const auto& c : cases) {
        const auto& j = c.j;
        CAPTURE(c.where);
        if (j.contains("invalid")) {
            CHECK(revisa::guard::invalid_output(j.at("segment").get<std::string>(), j.at("output").get<std::string>()) ==
                  j.at("invalid").get<bool>());
            continue;
        }
        const Marked m = parse_marked(j.at("marked").get<std::string>());
        const auto& types = j.at("types");
        REQUIRE(types.size() == m.edits.size());
        std::vector<Item> items;
        for (std::size_t i = 0; i < m.edits.size(); ++i) items.push_back(item(m.edits[i], type_of(types[i])));
        std::vector<revisa::protect::Span> spans;
        for (const auto& p : j.value("protected", nlohmann::json::array())) {
            const std::string s = p.get<std::string>();
            const std::size_t at = m.segment.find(s);
            REQUIRE_MESSAGE(at != std::string::npos, s);
            spans.push_back({at, at + s.size()});
        }
        std::set<revisa::catalog::Type> disabled;
        for (const auto& d : j.value("disabled", nlohmann::json::array())) disabled.insert(type_of(d));
        const std::string output = j.value("output", m.corrected);

        // The case files exercise the guard mechanics with their own thresholds (0.3, 1); the
        // calibrated defaults have their own test below.
        const revisa::guard::Outcome o =
            revisa::guard::apply(m.segment, output, items, spans, disabled, kCaseThresholds);
        if (j.contains("rejection")) {
            CHECK(std::string(rejection_name(o.rejection)) == j.at("rejection").get<std::string>());
            CHECK(o.kept.empty());
            continue;
        }
        CHECK(std::string(rejection_name(o.rejection)) == "None");
        std::vector<std::size_t> kept;
        for (const Item& k : o.kept) {
            std::size_t idx = m.edits.size();
            for (std::size_t i = 0; i < m.edits.size(); ++i)
                if (m.edits[i].start == k.edit.start && m.edits[i].end == k.edit.end && m.edits[i].to == k.edit.to)
                    idx = i;
            kept.push_back(idx);
        }
        CHECK(kept == j.at("kept").get<std::vector<std::size_t>>());
    }
}

TEST_CASE("guard: change_ratio counts the characters each edit changes (Levenshtein)") {
    const Marked m = parse_marked("Ya [e|he] [echo|hecho] el informe");
    CHECK(std::abs(revisa::guard::change_ratio(m.segment, m.edits) - 2.0 / 20.0) < 1e-12);
    CHECK(revisa::guard::change_ratio(m.segment, {}) == 0);
    const Marked n = parse_marked("[ñ|]é");
    CHECK(revisa::guard::change_ratio(n.segment, n.edits) == 0.5);
    CHECK(revisa::guard::kMaxChangeRatio > 0);
    CHECK(revisa::guard::kMaxOtro >= 0);
}

TEST_CASE("guard: calibrated defaults: one OTRO or a 25 % change leaves the segment") {
    const std::set<revisa::catalog::Type> none;
    const Marked one = parse_marked("Me [gusta|encanta] mucho la casa que habéis comprado");
    std::vector<Item> items{item(one.edits[0], revisa::catalog::Type::Otro)};
    CHECK(revisa::guard::apply(one.segment, one.corrected, items, {}, none).rejection ==
          revisa::guard::Rejection::TooManyOtro);
    CHECK(revisa::guard::apply(one.segment, one.corrected, items, {}, none, kCaseThresholds).rejection ==
          revisa::guard::Rejection::None);
    // 2 changed code points out of 8: 0.25 > 0.2.
    const Marked big = parse_marked("[e|he] [ab|av]cdef");
    std::vector<Item> two{item(big.edits[0], revisa::catalog::Type::VerboHaberH),
                          item(big.edits[1], revisa::catalog::Type::VerboHaberH)};
    CHECK(revisa::guard::change_ratio(big.segment, big.edits) > revisa::guard::kMaxChangeRatio);
    CHECK(revisa::guard::apply(big.segment, big.corrected, two, {}, none).rejection ==
          revisa::guard::Rejection::ChangeRatio);
}

TEST_CASE("guard: out_of_catalog on single edits") {
    const auto ooc = [](const std::string& marked) {
        const Marked m = parse_marked(marked);
        REQUIRE(m.edits.size() == 1);
        return revisa::guard::out_of_catalog(item(m.edits[0], revisa::catalog::Type::Otro), m.segment);
    };
    CHECK(ooc("Hola[|,] Ana"));
    CHECK(ooc("Hola Ana[|.]"));
    CHECK_FALSE(ooc("Hola[,|] Ana"));
    CHECK_FALSE(ooc("Hola Ana[.|]"));
    CHECK(ooc("Dijo [\"|«]hola\""));
    CHECK(ooc("Son [1000|1.000]"));
    CHECK(ooc("Es [URGENTE|urgente]"));
    CHECK_FALSE(ooc("Es [urgente|URGENTE]"));
    CHECK_FALSE(ooc("Ya [e|he] llegado"));
}
