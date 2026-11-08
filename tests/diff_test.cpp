// core/diff: tokenizer, the cases of tests/diff/*.jsonl (100 %) and the random apply property.
#include "doctest.h"

#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "core/diff/diff.hpp"
#include "core/text/text.hpp"
#include "jsonl.hpp"

using namespace revisa;
using diff::TokenKind;

namespace {

// "kind:text" per token, to compare in one CHECK.
std::vector<std::string> toks(std::string_view s) {
    std::vector<std::string> out;
    for (const diff::Token& t : diff::tokenize(s)) {
        const char* k = t.kind == TokenKind::Word ? "W" : t.kind == TokenKind::Number ? "N" : t.kind == TokenKind::Space ? "S" : "P";
        out.push_back(std::string(k) + ":" + std::string(s.substr(t.start, t.end - t.start)));
    }
    return out;
}

using V = std::vector<std::string>;

// Edits as JSON with UTF-16 offsets over the original (the shape of tests/diff/*.jsonl).
nlohmann::json edits_json(std::string_view original, const std::vector<diff::Edit>& edits) {
    const text::Utf16Index idx(original);
    nlohmann::json out = nlohmann::json::array();
    for (const diff::Edit& e : edits)
        out.push_back({{"start", idx.at(e.start)}, {"end", idx.at(e.end)}, {"from", e.from}, {"to", e.to}});
    return out;
}

// The invariants of diff() for one pair. Returns false (after reporting) on the first failure.
bool check_invariants(std::string_view o, std::string_view c, const std::vector<diff::Edit>& edits) {
    std::size_t prev_end = 0;
    bool prev_insertion_at = false;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const diff::Edit& e = edits[i];
        bool ok = e.start <= e.end && e.end <= o.size() && e.to_start <= e.to_end && e.to_end <= c.size() &&
                  e.from == o.substr(e.start, e.end - e.start) &&
                  e.to == c.substr(e.to_start, e.to_end - e.to_start) && e.from != e.to;
        if (i > 0) {
            // Sorted and not overlapping; two insertions at the same point would be ambiguous.
            ok = ok && e.start >= prev_end && !(prev_insertion_at && e.start == prev_end && e.start == e.end);
        }
        if (!ok) {
            FAIL_CHECK("bad edit " << i << " for [" << o << "] -> [" << c << "]");
            return false;
        }
        prev_end = e.end;
        prev_insertion_at = e.start == e.end;
    }
    if (diff::apply(o, edits) != c) {
        FAIL_CHECK("apply mismatch for [" << o << "] -> [" << c << "]");
        return false;
    }
    return true;
}

}  // namespace

TEST_CASE("diff tokenize: kinds") {
    CHECK(toks("") == V{});
    CHECK(toks("Hola, ¿qué tal?") == V{"W:Hola", "P:,", "S: ", "P:¿", "W:qué", "S: ", "W:tal", "P:?"});
    CHECK(toks("año  ñu\t\n x") == V{"W:año", "S:  ", "W:ñu", "S:\t\n ", "W:x"});
    CHECK(toks("1.000,50 €") == V{"N:1.000,50", "S: ", "P:€"});
    CHECK(toks("3, 4. 5.") == V{"N:3", "P:,", "S: ", "N:4", "P:.", "S: ", "N:5", "P:."});
    CHECK(toks("1..2") == V{"N:1", "P:.", "P:.", "N:2"});
}

TEST_CASE("diff tokenize: letters and digits glue (mp3, 1º, 3er)") {
    CHECK(toks("mp3 1º 3er 2ª") == V{"W:mp3", "S: ", "W:1º", "S: ", "W:3er", "S: ", "W:2ª"});
    CHECK(toks("COVID-19") == V{"W:COVID", "P:-", "N:19"});
    CHECK(toks("a1.5b") == V{"W:a1.5b"});
    CHECK(toks("a.1") == V{"W:a", "P:.", "N:1"});
}

TEST_CASE("diff tokenize: apostrophes only between letters") {
    CHECK(toks("O'Donnell rock’n’roll") == V{"W:O'Donnell", "S: ", "W:rock’n’roll"});
    CHECK(toks("'hola'") == V{"P:'", "W:hola", "P:'"});
    CHECK(toks("los 90'") == V{"W:los", "S: ", "N:90", "P:'"});
    CHECK(toks("l' amic") == V{"W:l", "P:'", "S: ", "W:amic"});
    CHECK(toks("e\xCC\x81'a") == V{"W:e\xCC\x81'a"});  // a mark ends a letter cluster
}

TEST_CASE("diff tokenize: combining marks, emoji and invalid bytes") {
    CHECK(toks("cafe\xCC\x81 bien") == V{"W:cafe\xCC\x81", "S: ", "W:bien"});
    CHECK(toks("\xCC\x81" "a") == V{"W:\xCC\x81" "a"});  // a lone mark starts a word
    CHECK(toks("5\xCC\x81") == V{"W:5\xCC\x81"});
    CHECK(toks("ok😀👍🏽!") == V{"W:ok", "P:😀", "P:👍", "P:🏽", "P:!"});
    CHECK(toks("a\xFF\xC3z") == V{"W:a", "P:\xFF", "P:\xC3", "W:z"});
    CHECK(toks("中文") == V{"P:中", "P:文"});  // outside the letter classes of core/text
}

TEST_CASE("diff tokenize: tokens cover the text contiguously") {
    const std::string s = "¡Hola! Ya e echo  el informe (1.000 €), ¿vale? 😀 \xFF fin";
    std::size_t pos = 0;
    for (const diff::Token& t : diff::tokenize(s)) {
        CHECK(t.start == pos);
        CHECK(t.end > t.start);
        pos = t.end;
    }
    CHECK(pos == s.size());
}

TEST_CASE("diff cases: tests/diff/*.jsonl (100 %)") {
    const auto cases = test::load_jsonl_dir("diff");
    REQUIRE(cases.size() >= 40);
    int failed = 0;
    for (const auto& c : cases) {
        const std::string o = c.j.at("original"), r = c.j.at("corrected");
        const auto edits = diff::diff(o, r);
        const nlohmann::json got = edits_json(o, edits);
        if (got != c.j.at("edits")) {
            ++failed;
            FAIL_CHECK(c.where << ": got " << got.dump());
        }
        if (!check_invariants(o, r, edits)) ++failed;
    }
    CHECK(failed == 0);
}

TEST_CASE("diff: invalid bytes are tokens of their own and count one UTF-16 unit") {
    const std::string o = "a\xFF b\xC3 😀 que";
    const auto e = diff::diff(o, "a b 😀 qué");
    REQUIRE(e.size() == 3);
    CHECK(e[0].from == "\xFF");
    CHECK(e[1].from == "\xC3");
    const text::Utf16Index idx(o);
    CHECK(idx.at(e[0].start) == 1);
    CHECK(idx.at(e[1].start) == 4);
    CHECK(idx.at(e[2].start) == 9);  // a, FF, space, b, C3, space, 😀 (2), space
    CHECK(e[2].from == "que");
    CHECK(check_invariants(o, "a b 😀 qué", e));
}

TEST_CASE("diff: the result does not depend on the alignment Myers picks") {
    // Deleting one of several equal words always takes the same «word + space».
    for (const char* o : {"de de que", "de de de que", "que que vino"}) {
        const std::string s(o);
        const auto e = diff::diff(s, s.substr(s.find(' ') + 1));
        REQUIRE(e.size() == 1);
        CHECK(e[0].start == 0);
        CHECK(e[0].from.back() == ' ');
    }
    const auto e = diff::diff("dijo que que vendría", "dijo que vendría");
    REQUIRE(e.size() == 1);
    CHECK(e[0].start == 5);
    CHECK(e[0].from == "que ");
}

TEST_CASE("diff: long texts stay fast (20.000 characters)") {
    std::string a, b;
    for (int i = 0; i < 2500; ++i) {
        a += "palabra" + std::to_string(i % 97) + " ";
        b += "otra" + std::to_string(i % 89) + ", ";
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto all = diff::diff(a, b);  // distance above the cap: one edit of the middle
    CHECK(check_invariants(a, b, all));
    std::string c = a;
    for (std::size_t p = 100; p + 20 < c.size(); p += 400) c.replace(p, 3, "xyz ");
    const auto few = diff::diff(a, c);
    CHECK(check_invariants(a, c, few));
    CHECK(few.size() >= 40);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(secs < 1.0);
}

TEST_CASE("diff property: apply(o, diff(o, c)) == c on 10.000 random pairs") {
    // std::mt19937 is fully specified by the standard; distributions are not, so plain modulo.
    std::mt19937 rng(20261104u);
    auto pick = [&rng](std::size_t n) { return static_cast<std::size_t>(rng() % n); };
    const std::vector<std::string> pool = {
        "que", "Qué", "qué", "de", "a", "ver", "haber", "he", "e", "echo", "hecho", "estás", "estas",
        "año", "Ñu", "pingüino", "o'clock", "1.000", "3,5", "42", "mp3", "1º", ",", ".", "¿", "?", "¡",
        "!", "(", ")", "«", "»", "-", "…", " ", " ", " ", "  ", "\t", "\n", " ", "😀", "👍🏽", "e\xCC\x81",
        "\xCC\x81", "\xFF", "\xC3", "’", "'", "中"};
    const auto t0 = std::chrono::steady_clock::now();
    int failed = 0;
    for (int n = 0; n < 10000 && failed < 5; ++n) {
        std::vector<std::string> o;
        const std::size_t len = pick(25);
        for (std::size_t i = 0; i < len; ++i) o.push_back(pool[pick(pool.size())]);
        std::vector<std::string> c = o;
        const std::size_t muts = pick(6);
        for (std::size_t m = 0; m < muts; ++m) {
            const std::size_t kind = pick(4);
            if (kind == 0 || c.empty()) {
                c.insert(c.begin() + static_cast<std::ptrdiff_t>(pick(c.size() + 1)), pool[pick(pool.size())]);
            } else if (kind == 1) {
                c.erase(c.begin() + static_cast<std::ptrdiff_t>(pick(c.size())));
            } else if (kind == 2) {
                c[pick(c.size())] = pool[pick(pool.size())];
            } else {
                const std::size_t i = pick(c.size());
                c.insert(c.begin() + static_cast<std::ptrdiff_t>(i), c[i]);  // duplicate (shift ambiguity)
            }
        }
        std::string os, cs;
        for (const auto& s : o) os += s;
        for (const auto& s : c) cs += s;
        if (!check_invariants(os, cs, diff::diff(os, cs))) ++failed;
        if (!check_invariants(cs, os, diff::diff(cs, os))) ++failed;
    }
    CHECK(failed == 0);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(secs < 2.0);
}
