// revisa end-to-end tests: the real binary with pipes or in a pseudo-terminal, inside a sandbox
// (temporary HOME and XDG_*, NO_COLOR, LANG=es_ES.UTF-8, controlled PATH, fake curl). Corrections
// come from the engine driven by REVISA_FAKE_* (built with REVISA_TEST_HOOKS).

#include "doctest.h"

#include <stdlib.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "pty_session.hpp"

namespace fs = std::filesystem;
using revisa::e2e::ExitStatus;
using revisa::e2e::PipeResult;
using revisa::e2e::PtySession;
using json = nlohmann::json;

namespace {

constexpr int kTimeoutMs = 20000;

// The download test model: what the fake curl serves (src.bin), its size and SHA-256.
const char* kFakeModel = "not a model";
const char* kFakeModelSize = "11";
const char* kFakeModelSha = "708811ccb1510c6d6c6e6379ef09be39bdbb0e7edcf44fefcca21c6228ee6d89";
const char* kFakeModelUrl = "https://example.invalid/m.gguf";
// The default file name inside the models directory (fetch::kDefaultModel.filename).
const char* kModelFile = "revisa-0.8b-Q4_K_M.gguf";

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool exited_with(const ExitStatus& s, int code) { return s.exited && s.code == code; }

// A sentence with one error and its correction (one B_V edit).
const char* const kWrong = "Te boy a llamar esta tarde.";
const char* const kRight = "Te voy a llamar esta tarde.";

// Own temporary directory with HOME, XDG dirs, bin/ and a controlled environment. Nothing of the
// test runner's environment is inherited.
struct Sandbox {
    std::string dir;
    std::map<std::string, std::string> vars;

    Sandbox() {
        std::string tmpl = (fs::temp_directory_path() / "revisa-e2e-XXXXXX").string();
        REQUIRE(mkdtemp(tmpl.data()) != nullptr);
        dir = tmpl;
        fs::create_directories(dir + "/bin");
        vars = {
            {"HOME", dir},
            {"XDG_CONFIG_HOME", dir + "/.config"},
            {"XDG_CACHE_HOME", dir + "/.cache"},
            {"XDG_DATA_HOME", dir + "/.local/share"},
            {"TMPDIR", dir},
            {"NO_COLOR", "1"},
            {"LANG", "es_ES.UTF-8"},
            {"TERM", "xterm"},
            {"PATH", dir + "/bin:/usr/bin:/bin"},
        };
        // A fake curl: any download attempt is logged to curl.log and never reaches the network.
        fs::copy_file(std::string(REVISA_E2E_DIR) + "/fake_curl.sh", path("bin/curl"));
        fs::permissions(path("bin/curl"), fs::perms::owner_all);
        vars["FAKE_CURL_SRC"] = path("src.bin");
        vars["FAKE_CURL_LOG"] = path("curl.log");
        write_file("src.bin", kFakeModel);
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    std::string path(const std::string& name) const { return dir + "/" + name; }
    bool exists(const std::string& name) const { return fs::exists(path(name)); }

    std::string write_file(const std::string& name, const std::string& text) const {
        fs::create_directories(fs::path(path(name)).parent_path());
        std::ofstream(path(name), std::ios::binary) << text;
        return path(name);
    }

    // The fake engine corrects these trimmed segments and returns the rest unchanged.
    void fake_map(const json& map) {
        vars["REVISA_FAKE_MAP"] = write_file("fake-map.json", map.dump());
    }

    // The test model the fake curl serves, instead of the real one.
    void test_model() {
        vars["REVISA_TEST_MODEL_URL"] = kFakeModelUrl;
        vars["REVISA_TEST_MODEL_SIZE"] = kFakeModelSize;
        vars["REVISA_TEST_MODEL_SHA256"] = kFakeModelSha;
    }

    std::string models_dir() const { return path(".local/share/revisa/models"); }

    std::vector<std::string> env() const {
        std::vector<std::string> out;
        for (const auto& [k, v] : vars) out.push_back(k + "=" + v);
        return out;
    }

    std::vector<std::string> argv(const std::vector<std::string>& args) const {
        std::vector<std::string> out{REVISA_BIN};
        out.insert(out.end(), args.begin(), args.end());
        return out;
    }

    std::unique_ptr<PtySession> launch(const std::vector<std::string>& args) const {
        return std::make_unique<PtySession>(argv(args), env());
    }

    PipeResult pipes(const std::vector<std::string>& args, const std::string& input = "") const {
        return revisa::e2e::run_with_pipes(argv(args), env(), input, kTimeoutMs);
    }
};

// The segment of case `id` in tests/golden/cases.json (a list of {"id", "segment"}).
std::string golden_segment(const std::string& id) {
    const std::string path = std::string(REVISA_GOLDEN_DIR) + "/cases.json";
    REQUIRE_MESSAGE(fs::exists(path), "missing " << path);
    for (const auto& c : json::parse(read_file(path)))
        if (c.value("id", "") == id) return c.at("segment").get<std::string>();
    FAIL("no case " << id << " in " << path);
    return {};
}

}  // namespace

TEST_CASE("check: stdin through a pipe is corrected, exit 1") {
    Sandbox sb;
    sb.fake_map({{"haber si vienes mañana", "a ver si vienes mañana"}});
    const PipeResult r = sb.pipes({"check"}, "  haber si vienes mañana\n\nOtra frase.\n");
    CHECK(exited_with(r.status, 1));
    CHECK(r.out == "  a ver si vienes mañana\n\nOtra frase.\n");
    CHECK(r.err.empty());
}

TEST_CASE("check: correct text, exit 0") {
    Sandbox sb;
    sb.fake_map({{"haber", "a ver"}});
    const PipeResult r = sb.pipes({"check"}, "Todo está bien.");
    CHECK(exited_with(r.status, 0));
    CHECK(r.out == "Todo está bien.");
}

TEST_CASE("check: two files with headers") {
    Sandbox sb;
    sb.fake_map({{kWrong, kRight}});
    const std::string right = std::string(kRight) + "\n";
    const std::string a = sb.write_file("a.txt", std::string(kWrong) + "\n");
    const std::string b = sb.write_file("b.txt", right);
    const PipeResult r = sb.pipes({"check", a, b});
    CHECK(exited_with(r.status, 1));
    CHECK(r.out == "==> " + a + " <==\n" + right + "\n==> " + b + " <==\n" + right);
}

TEST_CASE("check --json: one JSON object per input, with edits") {
    Sandbox sb;
    sb.fake_map({{kWrong, kRight}});
    PipeResult r = sb.pipes({"check", "--json"}, std::string(kWrong) + "\n");
    CHECK(exited_with(r.status, 1));
    json j;
    REQUIRE_NOTHROW(j = json::parse(r.out));
    CHECK(j["version"] == 1);
    CHECK(j["catalog_version"] == 1);
    CHECK(j["model"] == "revisa-0.8b");
    CHECK(j["corrected"] == std::string(kRight) + "\n");
    REQUIRE(j["edits"].size() == 1);
    CHECK(j["edits"][0] == json({{"start", 3}, {"end", 6}, {"from", "boy"}, {"to", "voy"}, {"type", "B_V"},
                                 {"explanation", j["edits"][0]["explanation"]}}));
    CHECK(j["edits"][0]["explanation"].is_string());
    CHECK(j["segments_skipped"] == 0);
    CHECK_FALSE(j.contains("file"));

    const std::string a = sb.write_file("a.txt", kRight);
    r = sb.pipes({"check", a, "--json"});
    CHECK(exited_with(r.status, 0));
    REQUIRE_NOTHROW(j = json::parse(r.out));
    CHECK(j["file"] == a);
}

TEST_CASE("check: disabled_types in the config") {
    Sandbox sb;
    sb.fake_map({{kWrong, kRight}});
    sb.write_file(".config/revisa/config", "disabled_types = B_V\n");
    const PipeResult r = sb.pipes({"check"}, kWrong);
    CHECK(exited_with(r.status, 0));
    CHECK(r.out == kWrong);
}

TEST_CASE("diff --json: the canonical edits of a pair, without a model") {
    Sandbox sb;  // no fake engine and no model: diff never needs one
    PipeResult r = sb.pipes({"diff", "--json", "Hola Ana, haber si mañana podemos vernos.",
                             "Hola Ana, a ver si mañana podemos vernos."});
    CHECK(exited_with(r.status, 1));
    json j;
    REQUIRE_NOTHROW(j = json::parse(r.out));
    REQUIRE(j["edits"].size() == 1);
    CHECK(j["edits"][0]["start"] == 10);
    CHECK(j["edits"][0]["end"] == 15);
    CHECK(j["edits"][0]["type"] == "HABER_A_VER");
    CHECK(r.err.empty());

    r = sb.pipes({"diff", "igual", "igual"});
    CHECK(exited_with(r.status, 0));
    CHECK(r.out == "Sin diferencias.\n");
}

TEST_CASE("diff --jsonl and classify --jsonl read stdin line by line") {
    Sandbox sb;
    PipeResult r = sb.pipes({"diff", "--jsonl"},
                            json({{"id", "c1"}, {"original", kWrong}, {"corrected", kRight}}).dump() + "\n" +
                                json({{"id", "c2"}, {"original", "igual"}, {"corrected", "igual"}}).dump() + "\n");
    CHECK(exited_with(r.status, 0));
    CHECK(r.out == "{\"id\":\"c1\",\"edits\":[{\"start\":3,\"end\":6,\"from\":\"boy\",\"to\":\"voy\",\"type\":\"B_V\","
                   "\"explanation\":" +
                       json::parse(r.out.substr(0, r.out.find('\n')))["edits"][0]["explanation"].dump() +
                       ",\"out_of_catalog\":false}]}\n{\"id\":\"c2\",\"edits\":[]}\n");

    r = sb.pipes({"diff", "--jsonl"}, "{\"original\": \"a\", \"corrected\": \"b\"}\nno es json\n");
    CHECK(exited_with(r.status, 2));
    CHECK(r.out.empty());
    CHECK(contains(r.err, "línea 2"));

    r = sb.pipes({"classify", "--jsonl"}, "{\"id\": 1, \"from\": \"gusta\", \"to\": \"encanta\"}\n");
    CHECK(exited_with(r.status, 0));
    CHECK(r.out == "{\"id\":1,\"type\":\"OTRO\",\"explanation\":null}\n");
}

TEST_CASE("usage: diff and classify errors exit 2 in Spanish") {
    Sandbox sb;
    PipeResult r = sb.pipes({"diff", "solo uno"});
    CHECK(exited_with(r.status, 2));
    CHECK(contains(r.err, "necesita dos textos"));
    r = sb.pipes({"classify"});
    CHECK(exited_with(r.status, 2));
    CHECK(contains(r.err, "--jsonl"));
    r = sb.pipes({"check", "--jsonl", "a.txt"});
    CHECK(exited_with(r.status, 2));
    CHECK(contains(r.err, "sin ficheros"));
    r = sb.pipes({"check", "--replay"});
    CHECK(exited_with(r.status, 2));
    CHECK(contains(r.err, "solo sirve con «revisa check --jsonl»"));
    r = sb.pipes({"check", "--no-guards"});
    CHECK(exited_with(r.status, 2));
    CHECK(contains(r.err, "--no-guards solo sirve con"));
}

TEST_CASE("check --jsonl: one line per case, in order; a bad line exits 2 after the previous ones") {
    Sandbox sb;
    const std::string period = "Mañana te llamo para hablar del proyecto";
    sb.fake_map({{kWrong, kRight}, {period, period + "."}});
    const std::string in = json({{"id", "a"}, {"text", kWrong}}).dump() + "\n" +
                           json({{"text", "Todo está bien."}}).dump() + "\n" +
                           json({{"id", 3}, {"text", period}}).dump() + "\n";
    PipeResult r = sb.pipes({"check", "--jsonl"}, in);
    CHECK(exited_with(r.status, 0));
    CHECK(r.err.empty());
    std::vector<json> lines;
    std::istringstream ss(r.out);
    for (std::string l; std::getline(ss, l);) lines.push_back(json::parse(l));
    REQUIRE(lines.size() == 3);
    CHECK(lines[0]["id"] == "a");
    CHECK(lines[0]["corrected"] == kRight);
    CHECK(lines[0]["edits"][0]["type"] == "B_V");
    CHECK(lines[0]["changed"] == true);
    CHECK_FALSE(lines[1].contains("id"));
    CHECK(lines[1]["changed"] == false);
    CHECK(lines[2]["id"] == 3);
    CHECK(lines[2]["corrected"] == period);  // guard 4: the added final period is dropped

    // --no-guards: the model's view.
    r = sb.pipes({"check", "--jsonl", "--no-guards"}, in);
    CHECK(exited_with(r.status, 0));
    CHECK(contains(r.out, "\"corrected\":\"" + period + ".\""));

    r = sb.pipes({"check", "--jsonl"}, json({{"text", kWrong}}).dump() + "\nno es json\n" + in);
    CHECK(exited_with(r.status, 2));
    REQUIRE(std::count(r.out.begin(), r.out.end(), '\n') == 1);
    CHECK(json::parse(r.out)["corrected"] == kRight);
    CHECK(contains(r.err, "línea 2 de la entrada: JSON no válido"));

    // An engine error is exit 2 too.
    sb.vars["REVISA_FAKE_ERROR"] = "decode_failed";
    r = sb.pipes({"check", "--jsonl"}, in);
    CHECK(exited_with(r.status, 2));
    CHECK(contains(r.err, "falló la inferencia"));
}

TEST_CASE("check --jsonl --replay: an external correction through the same path, without a model") {
    Sandbox sb;  // no fake engine and no model: --replay never needs one
    const std::string text = std::string(kWrong) + "\nMañana te llamo para hablar del proyecto";
    const std::string corrected = std::string(kRight) + "\nMañana te llamo para hablar del proyecto.";
    const PipeResult r = sb.pipes({"check", "--jsonl", "--replay"},
                                  json({{"id", "x"}, {"text", text}, {"corrected", corrected}}).dump() + "\n");
    CHECK(exited_with(r.status, 0));
    CHECK(r.err.empty());
    json j;
    REQUIRE_NOTHROW(j = json::parse(r.out));
    CHECK(j["id"] == "x");
    CHECK(j["corrected"] == std::string(kRight) + "\nMañana te llamo para hablar del proyecto");
    REQUIRE(j["edits"].size() == 1);
    CHECK(j["edits"][0]["type"] == "B_V");
    CHECK(j["segments_skipped"] == 0);
    CHECK(j["changed"] == true);
    CHECK(j["replay_dropped"] == 0);

    const PipeResult n = sb.pipes({"check", "--jsonl", "--replay", "--no-guards"},
                                  json({{"text", text}, {"corrected", corrected}}).dump() + "\n");
    CHECK(exited_with(n.status, 0));
    CHECK(json::parse(n.out)["corrected"] == corrected);
}

TEST_CASE("check: a missing file is a Spanish error, exit 2") {
    Sandbox sb;
    sb.fake_map(json::object());
    const PipeResult r = sb.pipes({"check", sb.path("no-existe.txt")});
    CHECK(exited_with(r.status, 2));
    CHECK(r.out.empty());
    CHECK(contains(r.err, "no se puede leer"));
    CHECK(contains(r.err, "no existe"));
}

TEST_CASE("usage: no subcommand and unknown flags exit 2 in Spanish") {
    Sandbox sb;
    PipeResult r = sb.pipes({});
    CHECK(exited_with(r.status, 2));
    CHECK(contains(r.err, "falta la orden"));
    r = sb.pipes({"check", "--nada"});
    CHECK(exited_with(r.status, 2));
    CHECK(contains(r.err, "opción desconocida"));
}

TEST_CASE("--print-prompt equals the golden prompt byte by byte") {
    Sandbox sb;
    const std::string segment = golden_segment("normal");
    const std::string golden_path = std::string(REVISA_GOLDEN_DIR) + "/prompt_normal.txt";
    REQUIRE_MESSAGE(fs::exists(golden_path), "missing " << golden_path);
    const PipeResult r = sb.pipes({"--print-prompt", segment});
    CHECK(exited_with(r.status, 0));
    CHECK(r.out == read_file(golden_path));
}

TEST_CASE("model: not downloaded and no terminal, the error names the URL") {
    Sandbox sb;
    const PipeResult r = sb.pipes({"check"}, "hola\n");
    CHECK(exited_with(r.status, 2));
    CHECK(r.out.empty());
    CHECK(contains(r.err, "no está descargado"));
    CHECK(contains(r.err, "https://huggingface.co/"));
    CHECK_FALSE(sb.exists("curl.log"));
}

TEST_CASE("model: the download offer answered yes downloads the file") {
    Sandbox sb;
    sb.test_model();
    const std::string text = sb.write_file("t.txt", "hola\n");
    auto s = sb.launch({"check", text});
    REQUIRE(s->wait_for("[S/n]", kTimeoutMs));
    CHECK(contains(s->output(), "no está descargado"));
    CHECK(contains(s->output(), kFakeModelUrl));
    s->write("s\n");
    // The fake file then fails to load in the real engine: a Spanish load error, exit 2.
    const ExitStatus st = s->wait_exit(kTimeoutMs);
    CHECK(exited_with(st, 2));
    CHECK(contains(s->output(), "Modelo descargado en"));
    CHECK(contains(s->output(), "no se pudo cargar el modelo"));
    CHECK(read_file(sb.models_dir() + "/" + kModelFile) == kFakeModel);
    CHECK(sb.exists("curl.log"));
}

TEST_CASE("model: the download offer answered no downloads nothing") {
    Sandbox sb;
    sb.test_model();
    const std::string text = sb.write_file("t.txt", "hola\n");
    auto s = sb.launch({"check", text});
    REQUIRE(s->wait_for("[S/n]", kTimeoutMs));
    s->write("n\n");
    const ExitStatus st = s->wait_exit(kTimeoutMs);
    CHECK(exited_with(st, 2));
    CHECK(contains(s->output(), "no se ha descargado el modelo"));
    CHECK_FALSE(fs::exists(sb.models_dir() + "/" + kModelFile));
    CHECK_FALSE(sb.exists("curl.log"));
}
