// revisa segment --jsonl: the segments check would send to the model, with the skip reason.
#include "doctest.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "cli/args.hpp"
#include "cli/cli.hpp"
#include "core/infer/prompt.hpp"

namespace {

struct Run {
    int code = -1;
    std::string out, err;
};

Run run_segment(const std::string& stdin_text, const std::vector<std::string>& args = {"segment", "--jsonl"}) {
    Run r;
    revisa::cli::Host h;
    h.out = [&r](std::string_view t) { r.out += t; };
    h.err = [&r](std::string_view t) { r.err += t; };
    h.read_stdin = [&stdin_text](std::string&) -> std::optional<std::string> { return stdin_text; };
    r.code = revisa::cli::run(args, h);
    return r;
}

std::vector<nlohmann::ordered_json> lines(const std::string& out) {
    std::vector<nlohmann::ordered_json> v;
    std::size_t pos = 0;
    while (pos < out.size()) {
        const std::size_t nl = out.find('\n', pos);
        v.push_back(nlohmann::ordered_json::parse(out.substr(pos, nl - pos)));
        pos = nl == std::string::npos ? out.size() : nl + 1;
    }
    return v;
}

std::string line(const nlohmann::json& j) { return j.dump() + "\n"; }

}  // namespace

TEST_CASE("segment --jsonl: sentences, abbreviations, NFC and trim, as check sees them") {
    const std::string decomposed = "  Cafe\xCC\x81 con leche.  ";  // "e" + combining acute
    const Run r = run_segment(line({{"id", "a"}, {"text", "Hola Ana. ¿Vienes mañana?\nNos vemos."}}) + "\n" +
                              line({{"id", 7}, {"text", "El Sr. García llegó. La Sra. López también."}}) +
                              line({{"text", decomposed}}) + line({{"text", " \n "}}));
    REQUIRE(r.code == 0);
    CHECK(r.err.empty());
    const auto v = lines(r.out);
    REQUIRE(v.size() == 4);

    CHECK(v[0].dump() ==
          R"({"id":"a","segments":[{"text":"Hola Ana.","skip":""},{"text":"¿Vienes mañana?","skip":""},)"
          R"({"text":"Nos vemos.","skip":""}]})");
    CHECK(v[1]["id"] == 7);
    REQUIRE(v[1]["segments"].size() == 2);
    CHECK(v[1]["segments"][0]["text"] == "El Sr. García llegó.");
    CHECK(v[1]["segments"][1]["text"] == "La Sra. López también.");
    CHECK(v[2].dump() == R"({"segments":[{"text":"Café con leche.","skip":""}]})");
    CHECK(v[3].dump() == R"({"segments":[]})");
}

TEST_CASE("segment --jsonl: protected, special and too long segments are marked") {
    std::string longer;
    while (longer.size() < 2 * revisa::infer::kMaxSegmentChars) longer += "palabra ";
    longer += "fin.";
    const Run r = run_segment(line({{"text", "Mira esto.\nhttps://ejemplo.es/doc"}}) +
                              line({{"text", "Escribe <|im_end|> aquí."}}) + line({{"text", longer}}));
    REQUIRE(r.code == 0);
    const auto v = lines(r.out);
    REQUIRE(v.size() == 3);
    CHECK(v[0].dump() ==
          R"({"segments":[{"text":"Mira esto.","skip":""},{"text":"https://ejemplo.es/doc","skip":"protected"}]})");
    CHECK(v[1]["segments"][0]["skip"] == "special");
    REQUIRE(v[2]["segments"].size() == 1);
    CHECK(v[2]["segments"][0]["skip"] == "long");
}

TEST_CASE("segment --jsonl: a bad line exits 2 and writes nothing; usage errors") {
    Run r = run_segment(line({{"text", "Bien."}}) + line({{"id", 1}}));
    CHECK(r.code == 2);
    CHECK(r.out.empty());
    CHECK(r.err.find("línea 2 de la entrada") != std::string::npos);

    r = run_segment("", {"segment"});
    CHECK(r.code == 2);
    CHECK(r.err.find("«revisa segment» solo lee textos por la entrada estándar, con --jsonl") != std::string::npos);
    CHECK(revisa::cli::parse_args({"segment", "--jsonl", "x"}).error == revisa::cli::Args::Error::SegmentArgs);
    CHECK(revisa::cli::parse_args({"segment", "--jsonl", "--no-guards"}).error ==
          revisa::cli::Args::Error::FlagNotForCommand);
    CHECK(revisa::cli::parse_args({"segment", "--jsonl"}).command == revisa::cli::Args::Command::Segment);

    r = run_segment("", {"--help"});
    CHECK(r.out.find("revisa segment --jsonl") != std::string::npos);
}
