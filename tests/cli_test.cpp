// CLI tests in-process: run() over an in-memory host, with a scripted engine.
#include "doctest.h"
#include "test_util.hpp"

#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "cli/args.hpp"
#include "cli/cli.hpp"
#include "core/catalog/catalog.hpp"
#include "core/fetch/model_info.hpp"
#include "core/infer/engine.hpp"
#include "core/infer/prompt.hpp"
#include "core/segment/segment.hpp"
#include "core/text/text.hpp"

namespace fs = std::filesystem;
using revisa::cli::Args;
using revisa::cli::Host;
using revisa::infer::EngineResult;

namespace {

// Corrections by trimmed segment; unknown segments come back unchanged. errors: segment -> code.
struct EngineScript {
    std::map<std::string, std::string> fixes;
    std::map<std::string, std::string> errors;
    std::vector<std::string> calls;
};

class ScriptEngine final : public revisa::infer::Engine {
public:
    explicit ScriptEngine(EngineScript& script) : script_(script) {}
    EngineResult correct(std::string_view segment) override {
        const std::string s(segment);
        script_.calls.push_back(s);
        EngineResult r;
        if (const auto e = script_.errors.find(s); e != script_.errors.end()) {
            r.error = e->second;
            r.error_detail = "detalle de prueba";
            return r;
        }
        const auto f = script_.fixes.find(s);
        r.text = f != script_.fixes.end() ? f->second : s;
        return r;
    }

private:
    EngineScript& script_;
};

// Temporary directory, removed at the end.
struct TempDir {
    std::string dir;
    TempDir() : dir(revisa::test::make_temp_dir("revisa-cli-")) {}
    ~TempDir() { revisa::test::remove_tree(dir); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    std::string write(const std::string& name, const std::string& text) const {
        const std::string p = dir + "/" + name;
        fs::create_directories(fs::path(p).parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return p;
    }
};

// An isolated environment (config, data dir, no env hooks) and an in-memory terminal.
struct Fixture {
    TempDir tmp;
#ifdef _WIN32
    // Never the real user's %APPDATA%, %LOCALAPPDATA% or profile; restored after the test.
    revisa::test::EnvGuard appdata{"APPDATA"};
    revisa::test::EnvGuard local_appdata{"LOCALAPPDATA"};
    revisa::test::EnvGuard profile{"USERPROFILE"};
#endif
    EngineScript script;
    std::string out, err, stdin_text;
    std::deque<std::string> answers;  // read_line; empty = EOF
    int engines_made = 0;
    bool inject_engine = true;
    bool tty = false;

    Fixture() {
#ifdef _WIN32
        // config at <tmp>/config\revisa\config, models at <tmp>/data\revisa\models.
        appdata.set(tmp.dir + "/config");
        local_appdata.set(tmp.dir + "/data");
        profile.set(tmp.dir);
#else
        revisa::test::set_env("XDG_CONFIG_HOME", tmp.dir + "/config");
        revisa::test::set_env("XDG_DATA_HOME", tmp.dir + "/data");
#endif
        for (const char* v : {"REVISA_FAKE_ERROR", "REVISA_FAKE_MAP", "REVISA_FAKE_OUTPUT", "REVISA_TEST_MODEL_URL",
                              "REVISA_TEST_MODEL_SIZE", "REVISA_TEST_MODEL_SHA256"})
            revisa::test::unset_env(v);
    }

    Host host() {
        Host h;
        h.out = [this](std::string_view t) { out += t; };
        h.err = [this](std::string_view t) { err += t; };
        h.read_line = [this]() -> std::optional<std::string> {
            if (answers.empty()) return std::nullopt;
            std::string a = answers.front();
            answers.pop_front();
            return a;
        };
        h.read_stdin = [this](std::string&) -> std::optional<std::string> { return stdin_text; };
        if (inject_engine) {
            h.make_engine = [this]() -> std::unique_ptr<revisa::infer::Engine> {
                ++engines_made;
                return std::make_unique<ScriptEngine>(script);
            };
        }
        h.stdin_tty = tty;
        h.stderr_tty = tty;
        return h;
    }

    int run(const std::vector<std::string>& args) {
        out.clear();
        err.clear();
        return revisa::cli::run(args, host());
    }
};

// A sentence with one error and its correction (one B_V edit, well under the change-ratio guard).
constexpr const char* kWrong = "Te boy a llamar esta tarde.";
constexpr const char* kRight = "Te voy a llamar esta tarde.";

nlohmann::ordered_json parse_line(const std::string& line) { return nlohmann::ordered_json::parse(line); }

std::vector<std::string> keys(const nlohmann::ordered_json& j) {
    std::vector<std::string> out;
    for (const auto& [k, v] : j.items()) out.push_back(k);
    return out;
}

}  // namespace

// --- Arguments ------------------------------------------------------------------------------

TEST_CASE("cli parse_args: both flag forms, files and --") {
    Args a = revisa::cli::parse_args({"check", "--model", "/m.gguf", "a.txt", "--threads=4", "--json", "--", "-b"});
    CHECK(a.error == Args::Error::None);
    CHECK(a.command == Args::Command::Check);
    CHECK(a.model == "/m.gguf");
    CHECK(a.threads == 4);
    CHECK(a.json);
    CHECK(a.files == std::vector<std::string>{"a.txt", "-b"});

    a = revisa::cli::parse_args({"--model=/x.gguf", "check"});
    CHECK(a.command == Args::Command::Check);
    CHECK(a.model == "/x.gguf");
    CHECK(a.files.empty());
}

TEST_CASE("cli parse_args: errors") {
    CHECK(revisa::cli::parse_args({}).error == Args::Error::NoCommand);
    Args a = revisa::cli::parse_args({"corrige"});
    CHECK(a.error == Args::Error::UnknownCommand);
    CHECK(a.bad == "corrige");
    a = revisa::cli::parse_args({"check", "--nada"});
    CHECK(a.error == Args::Error::UnknownFlag);
    CHECK(a.bad == "--nada");
    a = revisa::cli::parse_args({"check", "--model"});
    CHECK(a.error == Args::Error::MissingValue);
    CHECK(a.bad == "--model");
    CHECK(revisa::cli::parse_args({"check", "--model="}).error == Args::Error::MissingValue);
    for (const char* bad : {"0", "257", "-1", "abc", "4x", "1000"}) {
        CAPTURE(bad);
        a = revisa::cli::parse_args({"check", "--threads", bad});
        CHECK(a.error == Args::Error::BadThreads);
        CHECK(a.bad == bad);
    }
    CHECK(revisa::cli::parse_args({"check", "--threads", "256"}).threads == 256);
    CHECK(revisa::cli::parse_args({"--print-prompt", "x", "check"}).error == Args::Error::PrintPromptExtra);
    // An unknown flag wins over --help.
    CHECK(revisa::cli::parse_args({"--help", "--nada"}).error == Args::Error::UnknownFlag);
    CHECK(revisa::cli::parse_args({"-h"}).command == Args::Command::Help);
    CHECK(revisa::cli::parse_args({"--version"}).command == Args::Command::Version);
}

TEST_CASE("cli run: usage errors are in Spanish and exit 2") {
    Fixture f;
    CHECK(f.run({}) == 2);
    CHECK(f.err.find("falta la orden") != std::string::npos);
    CHECK(f.err.find("revisa --help") != std::string::npos);
    CHECK(f.run({"check", "--nada"}) == 2);
    CHECK(f.err.find("opción desconocida: --nada") != std::string::npos);
    CHECK(f.run({"check", "--threads"}) == 2);
    CHECK(f.err.find("falta el valor de --threads") != std::string::npos);
    CHECK(f.run({"check", "--threads=0"}) == 2);
    CHECK(f.err.find("--threads") != std::string::npos);
    CHECK(f.run({"revisa"}) == 2);
    CHECK(f.err.find("orden desconocida: revisa") != std::string::npos);
    CHECK(f.out.empty());
    CHECK(f.engines_made == 0);
}

TEST_CASE("cli run: --help and --version") {
    Fixture f;
    CHECK(f.run({"--help"}) == 0);
    CHECK(f.out.find("Uso: revisa check") != std::string::npos);
    CHECK(f.run({"--version"}) == 0);
    CHECK(f.out == std::string("revisa ") + REVISA_VERSION + " (contrato del modelo v" +
                       std::to_string(revisa::infer::kContractVersion) + ")\n");
}

TEST_CASE("cli run: --print-prompt writes the exact contract string") {
    Fixture f;
    const std::string text = "  Hola, ¿qué tal?\n";
    CHECK(f.run({"--print-prompt", text}) == 0);
    CHECK(f.out == revisa::infer::render_qwen35(text));
    CHECK(f.out.ends_with("</think>\n\n"));  // nothing added after the prompt
    CHECK(f.run({"--print-prompt=hola"}) == 0);
    CHECK(f.out == revisa::infer::render_qwen35("hola"));
    CHECK(f.engines_made == 0);
}

// --- revisa check ---------------------------------------------------------------------------

TEST_CASE("cli run check: stdin, exit codes 0 and 1") {
    Fixture f;
    f.script.fixes[kWrong] = kRight;
    f.stdin_text = std::string(kWrong) + "\n";
    CHECK(f.run({"check"}) == 1);
    CHECK(f.out == std::string(kRight) + "\n");
    CHECK(f.err.empty());

    f.stdin_text = "Todo bien.";
    CHECK(f.run({"check"}) == 0);
    CHECK(f.out == "Todo bien.");
}

TEST_CASE("cli run check: no engine is made when no segment needs the model") {
    Fixture f;
    f.stdin_text = "\n   \n";
    CHECK(f.run({"check"}) == 0);
    CHECK(f.out == "\n   \n");
    CHECK(f.engines_made == 0);
}

TEST_CASE("cli run check: engine errors exit 2 with a Spanish message") {
    Fixture f;
    f.script.errors["frase"] = "load_failed";
    f.stdin_text = "frase\n";
    CHECK(f.run({"check"}) == 2);
    CHECK(f.out.empty());
    CHECK(f.err.find("no se pudo cargar el modelo") != std::string::npos);

    f.script.errors["frase"] = "decode_failed";
    CHECK(f.run({"check"}) == 2);
    CHECK(f.err.find("falló la inferencia") != std::string::npos);

    // cap_reached is not an error: the line stays, exit 0.
    f.script.errors["frase"] = "cap_reached";
    CHECK(f.run({"check"}) == 0);
    CHECK(f.out == "frase\n");
}

TEST_CASE("cli run check: several files get head-like headers") {
    Fixture f;
    f.script.fixes[kWrong] = kRight;
    const std::string right = std::string(kRight) + "\n";
    const std::string a = f.tmp.write("a.txt", std::string(kWrong) + "\n");
    const std::string b = f.tmp.write("b.txt", right);
    CHECK(f.run({"check", a, b}) == 1);
    CHECK(f.out == "==> " + a + " <==\n" + right + "\n==> " + b + " <==\n" + right);
    CHECK(f.engines_made == 1);

    // A single file has no header.
    CHECK(f.run({"check", b}) == 0);
    CHECK(f.out == right);
}

TEST_CASE("cli run check: an unreadable file exits 2 before writing anything") {
    Fixture f;
    const std::string a = f.tmp.write("a.txt", "mal\n");
    CHECK(f.run({"check", a, f.tmp.dir + "/no-existe.txt"}) == 2);
    CHECK(f.out.empty());
    CHECK(f.err.find("no se puede leer " + f.tmp.dir + "/no-existe.txt: no existe") != std::string::npos);
}

TEST_CASE("cli run check --json: shape and key order") {
    Fixture f;
    f.script.fixes[kWrong] = kRight;
    f.script.errors["bucle"] = "cap_reached";
    f.stdin_text = std::string(kWrong) + "\nbucle\n";
    CHECK(f.run({"check", "--json"}) == 1);
    REQUIRE(f.out.ends_with("\n"));
    REQUIRE(f.out.find('\n') == f.out.size() - 1);  // one compact line
    const auto j = parse_line(f.out);
    CHECK(keys(j) == std::vector<std::string>{"version", "catalog_version", "model", "corrected", "edits",
                                              "segments_skipped"});
    CHECK(j["catalog_version"] == revisa::catalog::kCatalogVersion);
    CHECK(j["version"] == revisa::infer::kContractVersion);
    CHECK(j["model"] == revisa::fetch::kDefaultModel.name);
    CHECK(j["corrected"] == std::string(kRight) + "\nbucle\n");
    CHECK(j["segments_skipped"] == 1);
    REQUIRE(j["edits"].size() == 1);
    CHECK(keys(j["edits"][0]) == std::vector<std::string>{"start", "end", "from", "to", "type", "explanation"});
    CHECK(j["edits"][0]["from"] == "boy");
    CHECK(j["edits"][0]["to"] == "voy");
    CHECK(j["edits"][0]["type"] == "B_V");
}

TEST_CASE("cli run check --json: one line per file, with \"file\" first") {
    Fixture f;
    const std::string a = f.tmp.write("a.txt", "uno\n");
    const std::string b = f.tmp.write("b.txt", "dos");
    CHECK(f.run({"check", "--json", a, b, "--model", "/modelos/mio.gguf"}) == 0);
    std::vector<std::string> lines;
    for (std::size_t pos = 0; pos < f.out.size();) {
        const auto nl = f.out.find('\n', pos);
        REQUIRE(nl != std::string::npos);
        lines.push_back(f.out.substr(pos, nl - pos));
        pos = nl + 1;
    }
    REQUIRE(lines.size() == 2);
    const auto ja = parse_line(lines[0]);
    CHECK(keys(ja) == std::vector<std::string>{"file", "version", "catalog_version", "model", "corrected", "edits",
                                               "segments_skipped"});
    CHECK(ja["edits"] == nlohmann::ordered_json::array());
    CHECK(ja["file"] == a);
    CHECK(ja["model"] == "mio.gguf");
    CHECK(ja["corrected"] == "uno\n");
    CHECK(parse_line(lines[1])["file"] == b);
    CHECK(f.out.find("==>") == std::string::npos);
}

TEST_CASE("cli run check --json: invalid UTF-8 does not break the output") {
    Fixture f;
    f.stdin_text = "a\xff b\n";
    CHECK(f.run({"check", "--json"}) == 0);
    CHECK_NOTHROW(parse_line(f.out));
}

TEST_CASE("cli run check: a bad config exits 2") {
    Fixture f;
    f.tmp.write("config/revisa/config", "clave_rara = 1\n");
    f.stdin_text = "hola\n";
    CHECK(f.run({"check"}) == 2);
    CHECK(f.err.find("configuración") != std::string::npos);
    CHECK(f.engines_made == 0);
}

TEST_CASE("cli run check: a missing explicit model is an error, never downloaded") {
    Fixture f;
    f.inject_engine = false;
    f.tty = true;
    f.stdin_text = "hola\n";
    CHECK(f.run({"check", "--model", f.tmp.dir + "/no.gguf"}) == 2);
    CHECK(f.err.find("no existe el fichero del modelo") != std::string::npos);
    CHECK(f.err.find("[S/n]") == std::string::npos);
}

TEST_CASE("cli run check: without the model and without a terminal, the error names the URL") {
    Fixture f;
    f.inject_engine = false;
    f.stdin_text = "hola\n";
    CHECK(f.run({"check"}) == 2);
    CHECK(f.err.find(revisa::fetch::kDefaultModel.url) != std::string::npos);
    CHECK(f.err.find("no está descargado") != std::string::npos);
    CHECK(f.out.empty());

    // --json never asks, even in a terminal.
    f.tty = true;
    CHECK(f.run({"check", "--json"}) == 2);
    CHECK(f.err.find("[S/n]") == std::string::npos);
}

TEST_CASE("cli run check: declining the download (or EOF) exits 2 and downloads nothing") {
    Fixture f;
    f.inject_engine = false;
    f.tty = true;
    f.stdin_text = "hola\n";
    f.answers = {"n"};
    CHECK(f.run({"check"}) == 2);
    CHECK(f.err.find("¿Descargarlo ahora desde") != std::string::npos);
    CHECK(f.err.find("no se ha descargado el modelo") != std::string::npos);
    CHECK_FALSE(fs::exists(f.tmp.dir + "/data/revisa/models"));

    f.answers.clear();  // EOF
    CHECK(f.run({"check"}) == 2);
    CHECK(f.err.find("no se ha descargado el modelo") != std::string::npos);
}

// --- check --json with edits, disabled_types -------------------------------------------------

namespace {

const char* const kHaber = "Hola Ana, haber si mañana podemos vernos.";
const char* const kAVer = "Hola Ana, a ver si mañana podemos vernos.";

std::vector<std::string> split_lines(const std::string& out) {
    std::vector<std::string> lines;
    for (std::size_t pos = 0; pos < out.size();) {
        const auto nl = out.find('\n', pos);
        REQUIRE(nl != std::string::npos);
        lines.push_back(out.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

bool has(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

}  // namespace

TEST_CASE("cli check --json: edits follow the contract") {
    Fixture f;
    f.script.fixes[std::string("😀 ") + kHaber] = std::string("😀 ") + kAVer;  // the emoji is in the segment
    f.stdin_text = std::string("😀 ") + kHaber + "\n";
    CHECK(f.run({"check", "--json"}) == 1);
    const auto j = parse_line(f.out);
    CHECK(j["corrected"] == std::string("😀 ") + kAVer + "\n");
    REQUIRE(j["edits"].size() == 1);
    const auto& e = j["edits"][0];
    CHECK(e["start"] == 13);  // UTF-16: the emoji counts two units
    CHECK(e["end"] == 18);
    CHECK(e["from"] == "haber");
    CHECK(e["to"] == "a ver");
    CHECK(e["type"] == "HABER_A_VER");
    CHECK(e["explanation"].is_string());
    CHECK(j["segments_skipped"] == 0);
}

TEST_CASE("cli check: disabled_types in the config removes those edits from corrected") {
    Fixture f;
    f.tmp.write("config/revisa/config", "disabled_types = B_V, MAYUSCULA\n");
    f.script.fixes[kWrong] = kRight;
    f.stdin_text = kWrong;
    CHECK(f.run({"check"}) == 0);
    CHECK(f.out == kWrong);
    CHECK(f.run({"check", "--json"}) == 0);
    const auto j = parse_line(f.out);
    CHECK(j["corrected"] == kWrong);
    CHECK(j["edits"].empty());

    f.tmp.write("config/revisa/config", "disabled_types = B_V, TILDES\n");
    CHECK(f.run({"check"}) == 2);
    CHECK(has(f.err, "configuración"));
    CHECK(has(f.err, ":1: tipo desconocido en disabled_types: \"TILDES\""));
}

// --- revisa diff ------------------------------------------------------------------------------

TEST_CASE("cli diff --json: the edits of a pair, without the model") {
    Fixture f;
    CHECK(f.run({"diff", "--json", kHaber, kAVer}) == 1);
    REQUIRE(split_lines(f.out).size() == 1);
    const auto j = parse_line(f.out);
    CHECK(keys(j) == std::vector<std::string>{"edits"});
    REQUIRE(j["edits"].size() == 1);
    CHECK(keys(j["edits"][0]) == std::vector<std::string>{"start", "end", "from", "to", "type", "explanation"});
    CHECK(j["edits"][0]["start"] == 10);
    CHECK(j["edits"][0]["end"] == 15);
    CHECK(j["edits"][0]["type"] == "HABER_A_VER");

    CHECK(f.run({"diff", kHaber, kHaber, "--json"}) == 0);
    CHECK(f.out == "{\"edits\":[]}\n");
    CHECK(f.engines_made == 0);
}

TEST_CASE("cli diff: one readable line per edit") {
    Fixture f;
    CHECK(f.run({"diff", kHaber, kAVer}) == 1);
    CHECK(f.out.starts_with("10-15 «haber» → «a ver»  HABER_A_VER: "));
    CHECK(split_lines(f.out).size() == 1);
    CHECK(f.run({"diff", "Me gusta la casa.", "Me encanta la casa."}) == 1);
    CHECK(f.out == "3-8 «gusta» → «encanta»  OTRO: posible corrección\n");
    CHECK(f.run({"diff", "igual", "igual"}) == 0);
    CHECK(f.out == "Sin diferencias.\n");
    // "--" lets a text start with a dash.
    CHECK(f.run({"diff", "--", "-uno", "-uno"}) == 0);
    CHECK(f.engines_made == 0);
}

TEST_CASE("cli diff --jsonl: one line out per pair in, with the id first") {
    Fixture f;
    nlohmann::ordered_json a = {{"id", "a1"}, {"original", kHaber}, {"corrected", kAVer}};
    nlohmann::ordered_json b = {{"corrected", "igual"}, {"original", "igual"}, {"id", 7}};
    nlohmann::ordered_json c = {{"original", kWrong}, {"corrected", kRight}};
    f.stdin_text = a.dump() + "\n\n" + b.dump() + "\r\n" + c.dump();
    CHECK(f.run({"diff", "--jsonl"}) == 0);
    const auto lines = split_lines(f.out);
    REQUIRE(lines.size() == 3);
    const auto ja = parse_line(lines[0]);
    CHECK(keys(ja) == std::vector<std::string>{"id", "edits"});
    CHECK(ja["id"] == "a1");
    REQUIRE(ja["edits"].size() == 1);
    CHECK(ja["edits"][0]["type"] == "HABER_A_VER");
    CHECK(lines[1] == "{\"id\":7,\"edits\":[]}");
    const auto jc = parse_line(lines[2]);
    CHECK(keys(jc) == std::vector<std::string>{"edits"});
    CHECK(jc["edits"][0]["type"] == "B_V");
    CHECK(f.engines_made == 0);

    f.stdin_text = "";
    CHECK(f.run({"diff", "--jsonl"}) == 0);
    CHECK(f.out.empty());
}

TEST_CASE("cli diff --jsonl: a bad line is an error with its number and nothing is written") {
    Fixture f;
    const std::string good = nlohmann::json({{"original", "a"}, {"corrected", "b"}}).dump();
    f.stdin_text = good + "\n{\"original\": \"a\",\n";
    CHECK(f.run({"diff", "--jsonl"}) == 2);
    CHECK(f.out.empty());
    CHECK(has(f.err, "línea 2 de la entrada: JSON no válido"));

    f.stdin_text = good + "\n" + good + "\n{\"original\": \"a\"}\n";
    CHECK(f.run({"diff", "--jsonl"}) == 2);
    CHECK(has(f.err, "línea 3 de la entrada: falta el campo «corrected» o no es un texto"));

    f.stdin_text = "{\"original\": 1, \"corrected\": \"b\"}";
    CHECK(f.run({"diff", "--jsonl"}) == 2);
    CHECK(has(f.err, "línea 1 de la entrada: falta el campo «original»"));

    f.stdin_text = "[1, 2]";
    CHECK(f.run({"diff", "--jsonl"}) == 2);
    CHECK(has(f.err, "tiene que ser un objeto JSON"));

    f.stdin_text = "{\"original\": \"a\xff\", \"corrected\": \"b\"}";
    CHECK(f.run({"diff", "--jsonl"}) == 2);
    CHECK(has(f.err, "JSON no válido"));
}

// --- revisa classify --------------------------------------------------------------------------

TEST_CASE("cli classify --jsonl: type and explanation of each edit") {
    Fixture f;
    f.stdin_text = nlohmann::json({{"id", "x"}, {"from", "haber"}, {"to", "a ver"}}).dump() + "\n" +
                   nlohmann::json({{"from", "gusta"}, {"to", "encanta"}}).dump() + "\n" +
                   nlohmann::json({{"from", "de "}, {"to", ""}, {"left", "Creo "}, {"right", "que vienes."}}).dump() +
                   "\n";
    CHECK(f.run({"classify", "--jsonl"}) == 0);
    const auto lines = split_lines(f.out);
    REQUIRE(lines.size() == 3);
    const auto j0 = parse_line(lines[0]);
    CHECK(keys(j0) == std::vector<std::string>{"id", "type", "explanation"});
    CHECK(j0["type"] == "HABER_A_VER");
    CHECK(j0["explanation"].is_string());
    CHECK(lines[1] == "{\"type\":\"OTRO\",\"explanation\":null}");
    CHECK(parse_line(lines[2])["type"] == "DEQUEISMO");
    CHECK(f.engines_made == 0);

    f.stdin_text = "{\"from\": \"a\", \"to\": \"b\", \"left\": 3}\n";
    CHECK(f.run({"classify", "--jsonl"}) == 2);
    CHECK(has(f.err, "línea 1 de la entrada: falta el campo «left» o no es un texto"));
    CHECK(f.out.empty());
}

// --- Usage of diff and classify -------------------------------------------------------------

TEST_CASE("cli parse_args: diff and classify") {
    Args a = revisa::cli::parse_args({"diff", "--json", "uno", "dos"});
    CHECK(a.error == Args::Error::None);
    CHECK(a.command == Args::Command::Diff);
    CHECK(a.json);
    CHECK(a.texts == std::vector<std::string>{"uno", "dos"});
    a = revisa::cli::parse_args({"diff", "", ""});
    CHECK(a.command == Args::Command::Diff);  // empty texts are texts
    a = revisa::cli::parse_args({"diff", "--jsonl"});
    CHECK(a.command == Args::Command::Diff);
    CHECK(a.jsonl);
    a = revisa::cli::parse_args({"classify", "--jsonl"});
    CHECK(a.command == Args::Command::Classify);

    CHECK(revisa::cli::parse_args({"diff", "uno"}).error == Args::Error::DiffArgs);
    CHECK(revisa::cli::parse_args({"diff", "uno", "dos", "tres"}).error == Args::Error::DiffArgs);
    CHECK(revisa::cli::parse_args({"diff", "--jsonl", "uno"}).error == Args::Error::DiffArgs);
    CHECK(revisa::cli::parse_args({"classify"}).error == Args::Error::ClassifyArgs);
    CHECK(revisa::cli::parse_args({"classify", "--jsonl", "x"}).error == Args::Error::ClassifyArgs);
    a = revisa::cli::parse_args({"check", "--jsonl", "--threads=2", "--no-guards"});
    CHECK(a.error == Args::Error::None);
    CHECK(a.command == Args::Command::Check);
    CHECK(a.jsonl);
    CHECK(a.no_guards);
    CHECK(a.threads == 2);
    a = revisa::cli::parse_args({"check", "--jsonl", "--replay"});
    CHECK(a.error == Args::Error::None);
    CHECK(a.replay);
    CHECK(revisa::cli::parse_args({"check", "--json", "--no-guards"}).error == Args::Error::None);
    CHECK(revisa::cli::parse_args({"check", "--jsonl", "a.txt"}).error == Args::Error::CheckJsonlFiles);
    a = revisa::cli::parse_args({"check", "--json", "--jsonl"});
    CHECK(a.error == Args::Error::FlagNotForCommand);
    CHECK(a.bad == "--json");
    CHECK(a.command_word == "check --jsonl");
    a = revisa::cli::parse_args({"check", "--jsonl", "--replay", "--model", "m"});
    CHECK(a.error == Args::Error::FlagNotForCommand);
    CHECK(a.bad == "--model");
    CHECK(a.command_word == "check --replay");
    a = revisa::cli::parse_args({"check", "--replay"});
    CHECK(a.error == Args::Error::FlagNeeds);
    CHECK(a.bad == "--replay");
    a = revisa::cli::parse_args({"check", "--json", "--replay"});
    CHECK(a.error == Args::Error::FlagNeeds);
    a = revisa::cli::parse_args({"check", "--no-guards"});
    CHECK(a.error == Args::Error::FlagNeeds);
    CHECK(a.bad == "--no-guards");
    CHECK(revisa::cli::parse_args({"diff", "--jsonl", "--replay"}).error == Args::Error::FlagNotForCommand);
    CHECK(revisa::cli::parse_args({"diff", "--json", "--no-guards", "a", "b"}).error == Args::Error::FlagNotForCommand);
    CHECK(revisa::cli::parse_args({"classify", "--jsonl", "--no-guards"}).error == Args::Error::FlagNotForCommand);
    a = revisa::cli::parse_args({"diff", "--threads=2", "a", "b"});
    CHECK(a.error == Args::Error::FlagNotForCommand);
    CHECK(a.bad == "--threads");
    CHECK(revisa::cli::parse_args({"diff", "--json", "--jsonl"}).error == Args::Error::FlagNotForCommand);
    CHECK(revisa::cli::parse_args({"classify", "--jsonl", "--model", "m"}).error == Args::Error::FlagNotForCommand);
    CHECK(revisa::cli::parse_args({"classify", "--jsonl", "--json"}).error == Args::Error::FlagNotForCommand);
    // A value that looks like a flag is a value.
    CHECK(revisa::cli::parse_args({"check", "--model", "--jsonl"}).error == Args::Error::None);
}

TEST_CASE("cli run: usage errors of diff and classify exit 2 in Spanish") {
    Fixture f;
    CHECK(f.run({"diff", "uno"}) == 2);
    CHECK(has(f.err, "«revisa diff» necesita dos textos"));
    CHECK(has(f.err, "revisa --help"));
    CHECK(f.run({"classify"}) == 2);
    CHECK(has(f.err, "«revisa classify» solo lee ediciones por la entrada estándar, con --jsonl"));
    CHECK(f.run({"check", "--jsonl", "a.txt"}) == 2);
    CHECK(has(f.err, "«revisa check --jsonl» lee los casos por la entrada estándar, sin ficheros"));
    CHECK(f.run({"check", "--replay"}) == 2);
    CHECK(has(f.err, "la opción --replay solo sirve con «revisa check --jsonl»"));
    CHECK(f.run({"check", "--no-guards"}) == 2);
    CHECK(has(f.err, "la opción --no-guards solo sirve con «revisa check --json» o «revisa check --jsonl»"));
    CHECK(f.run({"check", "--jsonl", "--replay", "--threads", "2"}) == 2);
    CHECK(has(f.err, "la opción --threads no sirve con «revisa check --replay»"));
    CHECK(f.run({"diff", "--model", "/m.gguf", "a", "b"}) == 2);
    CHECK(has(f.err, "la opción --model no sirve con «revisa diff»"));
    CHECK(f.out.empty());
    CHECK(f.engines_made == 0);

    CHECK(f.run({"--help"}) == 0);
    CHECK(has(f.out, "revisa diff [--json] ORIGINAL CORREGIDO"));
    CHECK(has(f.out, "revisa classify --jsonl"));
    CHECK(has(f.out, "revisa check --jsonl --replay"));
    CHECK(has(f.out, "--no-guards"));
    CHECK(has(f.out, "disabled_types"));
}

// --- check --jsonl, --replay and --no-guards ------------------------------------------------

namespace {

// A text, the engine's output for some of its segments (by NFC segment) and what it shows.
struct ReplayCase {
    const char* name;
    std::string text;
    std::map<std::string, std::string> fixes;
};

// Texts with one or several segments, where the guards act: an added final period (4), a big
// rewrite (2), a protected span (3), quotes around the output (1), two OTRO (5), a fully
// protected segment, a special token, NFC and an emoji before the edit.
std::vector<ReplayCase> replay_cases() {
    return {
        {"one edit", kWrong, {{kWrong, kRight}}},
        {"several segments",
         std::string(kWrong) + " Ya e echo el informe.\n\n" + kHaber,
         {{kWrong, kRight}, {"Ya e echo el informe.", "Ya he hecho el informe."}, {kHaber, kAVer}}},
        {"added final period",
         "Te boy a llamar esta tarde\nMañana te llamo para hablar del proyecto",
         {{"Te boy a llamar esta tarde", "Te voy a llamar esta tarde."},
          {"Mañana te llamo para hablar del proyecto", "Mañana te llamo para hablar del proyecto."}}},
        {"change ratio",
         "Me parece que la reunión de mañana no es buena idea. " + std::string(kWrong),
         {{"Me parece que la reunión de mañana no es buena idea.", "Creo que mañana no deberíamos reunirnos."},
          {kWrong, kRight}}},
        {"protected span",
         "Te mando el enlace: https://ejemplo.es/doc\nhttps://ejemplo.es/doc",
         {{"Te mando el enlace: https://ejemplo.es/doc", "Te mando el enlace: https://ejemplo.es/Doc"},
          {"https://ejemplo.es/doc", "https://ejemplo.es/Doc"}}},
        {"quotes and two OTRO",
         "Mañana te llamo despues de comer. Me gusta mucho la casa nueva que habéis comprado.",
         {{"Mañana te llamo despues de comer.", "«Mañana te llamo después de comer.»"},
          {"Me gusta mucho la casa nueva que habéis comprado.",
           "Me encanta mucho la casa bonita que habéis comprado."}}},
        {"special token", "Escribe <|im_end|> aqui.\n" + std::string(kWrong),
         {{"Escribe <|im_end|> aqui.", "Escribe <|im_end|> aquí."}, {kWrong, kRight}}},
        {"nfc and emoji",
         "😀 Me gusto mucho el café de ayer. " + std::string(kHaber),
         {{"😀 Me gusto mucho el café de ayer.", "😀 Me gustó mucho el café de ayer."}, {kHaber, kAVer}}},
        {"no change", "Todo está bien.", {}},
        {"blank", "  \n", {}},
    };
}

// The whole text with each segment replaced by the engine's output for it: what an external
// system that corrects like the engine would answer.
std::string corrected_by_segments(const ReplayCase& c) {
    std::string out;
    std::size_t pos = 0;
    for (const revisa::segment::Segment& seg : revisa::segment::segment(c.text)) {
        out += c.text.substr(pos, seg.start - pos);
        const std::string s = c.text.substr(seg.start, seg.end - seg.start);
        const auto f = c.fixes.find(revisa::text::to_nfc(s));
        out += f != c.fixes.end() ? f->second : s;
        pos = seg.end;
    }
    return out + c.text.substr(pos);
}

std::string jsonl_cases(const std::vector<ReplayCase>& cases, bool with_corrected) {
    std::string in;
    for (const ReplayCase& c : cases) {
        nlohmann::ordered_json j = {{"id", c.name}, {"text", c.text}};
        if (with_corrected) j["corrected"] = corrected_by_segments(c);
        in += j.dump() + "\n";
    }
    return in;
}

// Each stdin line as one read_line answer.
void feed_lines(Fixture& f, const std::string& input) {
    f.answers.clear();
    for (std::size_t pos = 0; pos < input.size();) {
        auto nl = input.find('\n', pos);
        if (nl == std::string::npos) nl = input.size();
        f.answers.push_back(input.substr(pos, nl - pos));
        pos = nl + 1;
    }
}

}  // namespace

TEST_CASE("cli check --jsonl: the same as check --json, text by text, with the model made once") {
    Fixture f;
    const auto cases = replay_cases();
    for (const ReplayCase& c : cases) f.script.fixes.insert(c.fixes.begin(), c.fixes.end());
    for (const bool guards : {true, false}) {
        CAPTURE(guards);
        f.engines_made = 0;
        feed_lines(f, jsonl_cases(cases, false));
        std::vector<std::string> args{"check", "--jsonl"};
        if (!guards) args.push_back("--no-guards");
        CHECK(f.run(args) == 0);
        CHECK(f.err.empty());
        CHECK(f.engines_made == 1);
        const auto lines = split_lines(f.out);
        REQUIRE(lines.size() == cases.size());
        for (std::size_t i = 0; i < cases.size(); ++i) {
            CAPTURE(cases[i].name);
            const auto jl = parse_line(lines[i]);
            CHECK(keys(jl) == std::vector<std::string>{"id", "corrected", "edits", "segments_skipped", "changed"});
            CHECK(jl["id"] == cases[i].name);
            f.stdin_text = cases[i].text;
            std::vector<std::string> json_args{"check", "--json"};
            if (!guards) json_args.push_back("--no-guards");
            const int code = f.run(json_args);
            const auto jj = parse_line(f.out);
            CHECK(jl["corrected"] == jj["corrected"]);
            CHECK(jl["edits"] == jj["edits"]);
            CHECK(jl["segments_skipped"] == jj["segments_skipped"]);
            CHECK(jl["changed"] == (code == 1));
        }
    }
}

TEST_CASE("cli check --jsonl --replay: the engine's own output replayed gives the same result") {
    Fixture f;
    const auto cases = replay_cases();
    for (const ReplayCase& c : cases) f.script.fixes.insert(c.fixes.begin(), c.fixes.end());
    for (const bool guards : {true, false}) {
        CAPTURE(guards);
        std::vector<std::string> args{"check", "--jsonl"};
        if (!guards) args.push_back("--no-guards");
        feed_lines(f, jsonl_cases(cases, false));
        REQUIRE(f.run(args) == 0);
        const auto expected = split_lines(f.out);

        f.engines_made = 0;
        args.push_back("--replay");
        feed_lines(f, jsonl_cases(cases, true));
        REQUIRE(f.run(args) == 0);
        CHECK(f.engines_made == 0);
        const auto lines = split_lines(f.out);
        REQUIRE(lines.size() == expected.size());
        for (std::size_t i = 0; i < lines.size(); ++i) {
            CAPTURE(cases[i].name);
            auto j = parse_line(lines[i]);
            CHECK(keys(j).back() == "replay_dropped");
            CHECK(j["replay_dropped"] == 0);
            j.erase("replay_dropped");
            CHECK(j.dump() == parse_line(expected[i]).dump());
        }
    }
}

TEST_CASE("cli check --jsonl --replay: what crosses a segment boundary is dropped and counted") {
    Fixture f;
    feed_lines(f, nlohmann::json({{"text", "Vale.\nAdiós."}, {"corrected", "Vale adiós."}}).dump() + "\n" +
                      // Whitespace: the edges and between segments do not count.
                      nlohmann::json({{"text", std::string(kWrong) + "  Adiós.\n"},
                                      {"corrected", std::string(" ") + kRight + " Adiós."}})
                          .dump());
    CHECK(f.run({"check", "--jsonl", "--replay"}) == 0);
    const auto lines = split_lines(f.out);
    REQUIRE(lines.size() == 2);
    const auto j0 = parse_line(lines[0]);
    CHECK(j0["replay_dropped"] == 1);  // ".\n" -> " " crosses; «Adiós» -> «adiós» goes to its segment,
    // where it is an OTRO edit: with the default kMaxOtro = 0 that segment stays unchanged.
    CHECK(j0["corrected"] == "Vale.\nAdiós.");
    const auto j1 = parse_line(lines[1]);
    CHECK(j1["replay_dropped"] == 0);
    CHECK(j1["corrected"] == std::string(kRight) + "  Adiós.\n");
    CHECK(j1["changed"] == true);
}

TEST_CASE("cli check --no-guards: lets through what the guards drop") {
    Fixture f;
    const std::string period = "Mañana te llamo para hablar del proyecto";
    const std::string rewrite = "Me parece que la reunión de mañana no es buena idea.";
    const std::string url = "Te mando el enlace: https://ejemplo.es/doc";
    f.script.fixes[period] = period + ".";
    f.script.fixes[rewrite] = "Creo que mañana no deberíamos reunirnos.";
    f.script.fixes[url] = "Te mando el enlace: https://ejemplo.es/Doc";
    f.script.fixes[kWrong] = kRight;
    f.tmp.write("config/revisa/config", "disabled_types = B_V\n");
    for (const std::string& text : {period, rewrite, url, std::string(kWrong)}) {
        CAPTURE(text);
        f.stdin_text = text;
        CHECK(f.run({"check", "--json"}) == 0);
        auto j = parse_line(f.out);
        CHECK(j["corrected"] == text);
        CHECK(j["edits"].empty());
        CHECK(f.run({"check", "--json", "--no-guards"}) == 1);
        j = parse_line(f.out);
        CHECK(j["corrected"] == f.script.fixes[text]);
        CHECK_FALSE(j["edits"].empty());
        CHECK(j["segments_skipped"] == 0);
    }
    // Edits are classified the same: B_V, even though the config disables it.
    f.stdin_text = kWrong;
    CHECK(f.run({"check", "--json", "--no-guards"}) == 1);
    CHECK(parse_line(f.out)["edits"][0]["type"] == "B_V");
}

TEST_CASE("cli check --jsonl: errors exit 2, with the previous lines already written") {
    Fixture f;
    f.script.fixes[kWrong] = kRight;
    const std::string good = nlohmann::json({{"id", 1}, {"text", kWrong}}).dump();
    struct Bad {
        std::string line;
        std::string message;
    };
    for (const Bad& b : std::vector<Bad>{
             {"{\"text\": \"a\",", "línea 3 de la entrada: JSON no válido"},
             {"[1]", "línea 3 de la entrada: tiene que ser un objeto JSON"},
             {"{\"id\": 2}", "línea 3 de la entrada: falta el campo «text» o no es un texto"},
             {"{\"text\": 5}", "línea 3 de la entrada: falta el campo «text» o no es un texto"},
         }) {
        CAPTURE(b.line);
        feed_lines(f, good + "\n\n" + b.line + "\n" + good);
        CHECK(f.run({"check", "--jsonl"}) == 2);
        CHECK(split_lines(f.out).size() == 1);
        CHECK(has(f.err, b.message));
    }
    feed_lines(f, nlohmann::json({{"text", kWrong}, {"corrected", kRight}}).dump() + "\n" +
                      nlohmann::json({{"text", kWrong}}).dump());
    CHECK(f.run({"check", "--jsonl", "--replay"}) == 2);
    CHECK(split_lines(f.out).size() == 1);
    CHECK(has(f.err, "línea 2 de la entrada: falta el campo «corrected» o no es un texto"));

    // An engine error is an error too; cap_reached is not.
    f.script.errors["frase"] = "decode_failed";
    feed_lines(f, good + "\n" + nlohmann::json({{"text", "frase"}}).dump());
    CHECK(f.run({"check", "--jsonl"}) == 2);
    CHECK(split_lines(f.out).size() == 1);
    CHECK(has(f.err, "falló la inferencia"));
    f.script.errors["frase"] = "cap_reached";
    feed_lines(f, good + "\n" + nlohmann::json({{"text", "frase"}}).dump());
    CHECK(f.run({"check", "--jsonl"}) == 0);
    const auto lines = split_lines(f.out);
    REQUIRE(lines.size() == 2);
    CHECK(parse_line(lines[1])["segments_skipped"] == 1);

    // No line, no output and no engine.
    f.engines_made = 0;
    feed_lines(f, "");
    CHECK(f.run({"check", "--jsonl"}) == 0);
    CHECK(f.out.empty());
    CHECK(f.engines_made == 0);
}

TEST_CASE("cli diff --jsonl: out_of_catalog is guard 4 over the whole pair") {
    Fixture f;
    struct Pair {
        std::string original, corrected;
        bool out;
    };
    const std::vector<Pair> pairs{
        {"Sí lo sé.", "Sí, lo sé.", true},                        // added comma
        {"Nos vemos mañana", "Nos vemos mañana.", true},          // added final period
        {"Lo hice solo por ti.", "Lo hice sólo por ti.", true},   // admitted variant
        {kWrong, kRight, false},
        {kHaber, kAVer, false},
    };
    std::string in;
    for (const Pair& p : pairs) in += nlohmann::json({{"original", p.original}, {"corrected", p.corrected}}).dump() + "\n";
    f.stdin_text = in;
    CHECK(f.run({"diff", "--jsonl"}) == 0);
    const auto lines = split_lines(f.out);
    REQUIRE(lines.size() == pairs.size());
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        CAPTURE(pairs[i].corrected);
        const auto j = parse_line(lines[i]);
        REQUIRE(j["edits"].size() == 1);
        CHECK(keys(j["edits"][0]) ==
              std::vector<std::string>{"start", "end", "from", "to", "type", "explanation", "out_of_catalog"});
        CHECK(j["edits"][0]["out_of_catalog"] == pairs[i].out);
    }
    // diff --json does not change.
    CHECK(f.run({"diff", "--json", "Sí lo sé.", "Sí, lo sé."}) == 1);
    CHECK_FALSE(parse_line(f.out)["edits"][0].contains("out_of_catalog"));
}
