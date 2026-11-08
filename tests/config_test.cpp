#include "doctest.h"

#include "core/config/config.hpp"
#include "test_util.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace revisa::config;

namespace {

const char* const kFile = "~/.config/revisa/config";

ConfigResult parse(std::string_view text, std::string_view home = "/home/ana") {
    return parse_config(text, kFile, home);
}

// Checks that it fails on the given line and returns the result to inspect the message.
ConfigResult parse_err(std::string_view text, int line) {
    ConfigResult r = parse(text);
    CHECK_FALSE(r.config.has_value());
    const std::string prefix = std::string(kFile) + ":" + std::to_string(line) + ": ";
    CHECK(r.error.rfind(prefix, 0) == 0);
    return r;
}

bool contains(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }

using revisa::test::EnvGuard;

// Own temporary directory, removed on exit.
struct TempDir : revisa::test::TempDir {
    TempDir() : revisa::test::TempDir("revisa_config_test_") {}
};

void write_file(const std::string& p, const std::string& text) {
    std::filesystem::create_directories(std::filesystem::path(p).parent_path());
    std::ofstream out(p, std::ios::binary);
    out << text;
}

std::string read_file(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// save_config, then the file parsed back.
Config save_and_load(const std::string& p, const Config& cfg) {
    std::string error;
    REQUIRE(save_config(p, cfg, &error));
    CHECK(error.empty());
    const ConfigResult r = parse_config(read_file(p), p, "/home/ana");
    REQUIRE(r.config.has_value());
    return *r.config;
}

}  // namespace

TEST_CASE("config: empty text and comments only give the defaults") {
    for (std::string_view text : {"", "\n\n", "# comment\n  # indented\n", "\t \r\n"}) {
        const ConfigResult r = parse(text);
        REQUIRE(r.config.has_value());
        CHECK(r.error.empty());
        CHECK(r.config->model.empty());
        CHECK(r.config->threads == 0);
    }
}

TEST_CASE("config: valid file with spaces, tabs, CRLF and comments") {
    const ConfigResult r = parse("# revisa\r\n  model\t=  /opt/m.gguf \r\n\nthreads=8\r\n# end");
    REQUIRE(r.config.has_value());
    CHECK(r.config->model == "/opt/m.gguf");
    CHECK(r.config->threads == 8);
}

TEST_CASE("config: the first = splits key and value") {
    const ConfigResult r = parse("model = /a=b.gguf");
    REQUIRE(r.config.has_value());
    CHECK(r.config->model == "/a=b.gguf");
}

TEST_CASE("config: leading BOM is ignored") {
    const ConfigResult r = parse("\xEF\xBB\xBFthreads = 2\n");
    REQUIRE(r.config.has_value());
    CHECK(r.config->threads == 2);
}

TEST_CASE("config: ~/ expansion in model") {
    CHECK(parse("model = ~/m.gguf", "/home/ana").config->model == "/home/ana/m.gguf");
    CHECK(parse("model = ~/m.gguf", "/home/ana/").config->model == "/home/ana/m.gguf");
    CHECK(parse("model = ~x/m.gguf").config->model == "~x/m.gguf");  // only "~/"
    const ConfigResult r = parse_config("model = ~/m.gguf", kFile, "");
    CHECK_FALSE(r.config.has_value());
    CHECK(r.error == std::string(kFile) + ":1: no se puede expandir \"~/\" en model: HOME no está definida");
    CHECK_FALSE(parse_config("model = ~/m.gguf", kFile, "/").config.has_value());
}

TEST_CASE("config: threads range") {
    CHECK(parse("threads = 1").config->threads == 1);
    CHECK(parse("threads = 256").config->threads == kMaxThreads);
    CHECK(parse("threads = 0007").config->threads == 7);
    for (std::string_view bad : {"threads = 0", "threads = 257", "threads = -1", "threads = +4", "threads = 4x",
                                 "threads = 1 2", "threads = 99999999999999999999"}) {
        const ConfigResult r = parse_err(bad, 1);
        CHECK(contains(r.error, "valor no válido para threads"));
        CHECK(contains(r.error, "entero de 1 a 256"));
    }
}

TEST_CASE("config: error format for unknown key, repeated key, missing = and empty value") {
    CHECK(parse_err("\n# x\nmode = auto", 3).error ==
          std::string(kFile) + ":3: clave desconocida: \"mode\" (válidas: model, threads, disabled_types, port, hotkey)");
    CHECK(parse_err("threads = 2\n\nthreads = 3", 3).error ==
          std::string(kFile) + ":3: clave repetida: threads (ya definida en la línea 1)");
    CHECK(parse_err("model /x", 1).error == std::string(kFile) + ":1: falta \"=\" (formato: clave = valor)");
    CHECK(parse_err("model =   ", 1).error == std::string(kFile) + ":1: valor vacío para model");
    // The line number counts after a BOM too.
    CHECK(parse_err("\xEF\xBB\xBF\nfoo = 1", 2).error.size() > 0);
}

TEST_CASE("config: disabled_types takes catalog ids separated by commas and/or spaces") {
    using revisa::catalog::Type;
    CHECK(parse("").config->disabled_types.empty());
    ConfigResult r = parse("disabled_types = MAYUSCULA, ESPACIADO");
    REQUIRE(r.config.has_value());
    CHECK(r.config->disabled_types == std::set<Type>{Type::Mayuscula, Type::Espaciado});
    r = parse("disabled_types=OTRO,COMA_SOBRA  LEISMO,,\tOTRO");
    REQUIRE(r.config.has_value());
    CHECK(r.config->disabled_types == std::set<Type>{Type::Otro, Type::ComaSobra, Type::Leismo});
    CHECK(parse_err("threads = 2\ndisabled_types = MAYUSCULA, Tildes", 2).error ==
          std::string(kFile) +
              ":2: tipo desconocido en disabled_types: \"Tildes\" (válidos: los ids del catálogo, como MAYUSCULA o "
              "ESPACIADO)");
    CHECK(contains(parse_err("disabled_types = mayuscula", 1).error, "\"mayuscula\""));  // ids are exact
    CHECK(parse_err("disabled_types = , ,", 1).error == std::string(kFile) + ":1: valor vacío para disabled_types");
    CHECK(contains(parse_err("disabled_types = OTRO\ndisabled_types = OTRO", 2).error, "clave repetida"));
}

TEST_CASE("config: quoted values never carry control characters or invalid UTF-8") {
    const ConfigResult r = parse_err("k\x1b[31m\xff\xc2\x85\"\\ñ = 1", 1);
    CHECK(r.error.find('\x1b') == std::string::npos);
    CHECK(contains(r.error, "\"k\\x1b[31m\\xff\\xc2\\x85\\\"\\\\ñ\""));
    const ConfigResult t = parse_err("threads = 1\x07", 1);
    CHECK(contains(t.error, "\"1\\x07\""));
}

#ifdef _WIN32

TEST_CASE("config_path: APPDATA") {
    EnvGuard appdata("APPDATA");
    appdata.set("C:\\Users\\ana\\AppData\\Roaming\\");
    CHECK(config_path() == "C:\\Users\\ana\\AppData\\Roaming\\revisa\\config");
    appdata.set("C:/Users/ana//");
    CHECK(config_path() == "C:/Users/ana\\revisa\\config");
    appdata.unset();
    CHECK(config_path().empty());
}

TEST_CASE("load_config: missing file, valid file, errors with ~ and directory") {
    TempDir tmp;
    EnvGuard appdata("APPDATA");
    EnvGuard home("USERPROFILE");
    home.set(tmp.path);
    appdata.set(tmp.path + "\\AppData\\Roaming");
    const std::string file = tmp.path + "\\AppData\\Roaming\\revisa\\config";
    const std::string shown = "~\\AppData\\Roaming\\revisa\\config";

    ConfigResult r = load_config();
    REQUIRE(r.config.has_value());
    CHECK(r.config->threads == 0);

    write_file(file, "model = ~/m.gguf\nthreads = 3\n");
    r = load_config();
    REQUIRE(r.config.has_value());
    CHECK(r.config->model == tmp.path + "/m.gguf");
    CHECK(r.config->threads == 3);

    write_file(file, "threads = 3\nfoo = 1\n");
    r = load_config();
    CHECK_FALSE(r.config.has_value());
    CHECK(r.error.rfind(shown + ":2: clave desconocida", 0) == 0);

    std::filesystem::remove(file);
    std::filesystem::create_directories(file);
    r = load_config();
    CHECK_FALSE(r.config.has_value());
    CHECK(r.error == shown + ": no se puede leer: es un directorio");

    // Outside USERPROFILE the full path is shown.
    TempDir other;
    appdata.set(other.path);
    write_file(other.path + "\\revisa\\config", "threads = x\n");
    r = load_config();
    CHECK_FALSE(r.config.has_value());
    CHECK(r.error.rfind(other.path + "\\revisa\\config:1: ", 0) == 0);
}

#else

TEST_CASE("config_path: XDG_CONFIG_HOME and HOME") {
    EnvGuard xdg("XDG_CONFIG_HOME");
    EnvGuard home("HOME");
    home.set("/home/ana/");
    xdg.set("/xdg//");
    CHECK(config_path() == "/xdg/revisa/config");
    xdg.set("");
    CHECK(config_path() == "/home/ana/.config/revisa/config");
    xdg.unset();
    CHECK(config_path() == "/home/ana/.config/revisa/config");
    home.unset();
    CHECK(config_path().empty());
}

TEST_CASE("load_config: missing file, valid file, errors with ~ and directory") {
    TempDir tmp;
    EnvGuard xdg("XDG_CONFIG_HOME");
    EnvGuard home("HOME");
    xdg.unset();
    home.set(tmp.path);
    const std::string file = tmp.path + "/.config/revisa/config";

    ConfigResult r = load_config();
    REQUIRE(r.config.has_value());
    CHECK(r.config->threads == 0);

    write_file(file, "model = ~/m.gguf\nthreads = 3\n");
    r = load_config();
    REQUIRE(r.config.has_value());
    CHECK(r.config->model == tmp.path + "/m.gguf");
    CHECK(r.config->threads == 3);

    write_file(file, "threads = 3\nfoo = 1\n");
    r = load_config();
    CHECK_FALSE(r.config.has_value());
    CHECK(r.error.rfind("~/.config/revisa/config:2: clave desconocida", 0) == 0);

    std::filesystem::remove(file);
    std::filesystem::create_directories(file);
    r = load_config();
    CHECK_FALSE(r.config.has_value());
    CHECK(r.error == "~/.config/revisa/config: no se puede leer: es un directorio");

    // Outside HOME the full path is shown.
    TempDir other;
    xdg.set(other.path);
    write_file(other.path + "/revisa/config", "threads = x\n");
    r = load_config();
    CHECK_FALSE(r.config.has_value());
    CHECK(r.error.rfind(other.path + "/revisa/config:1: ", 0) == 0);
}

#endif  // _WIN32

// --- hotkey and save_config --------------------------------------------------------------------

TEST_CASE("config: hotkey defaults to Ctrl+Alt+Space and rejects control characters") {
    CHECK(parse("").config->hotkey == "Ctrl+Alt+Space");
    CHECK(parse("hotkey =  Meta+Shift+R \r\n").config->hotkey == "Meta+Shift+R");
    CHECK(parse("hotkey = Ctrl+#").config->hotkey == "Ctrl+#");
    CHECK(contains(parse_err("hotkey = Ctrl+\x1b", 1).error, "valor no válido para hotkey: \"Ctrl+\\x1b\""));
    CHECK(contains(parse_err("hotkey = Ctrl+\tA", 1).error, "valor no válido para hotkey"));
    CHECK(parse_err("hotkey =", 1).error == std::string(kFile) + ":1: valor vacío para hotkey");
}

TEST_CASE("save_config: a missing file and directories are created, with only the non-defaults") {
    using revisa::catalog::Type;
    TempDir tmp;
    const std::string file = tmp.path + "/a/b/config";
    std::string error;
    REQUIRE(save_config(file, Config{}, &error));
    CHECK(read_file(file).empty());

    Config cfg;
    cfg.model = "/opt/m.gguf";
    cfg.threads = 6;
    cfg.disabled_types = {Type::Espaciado, Type::Mayuscula};
    cfg.hotkey = "Ctrl+Shift+R";
    CHECK(save_and_load(file, cfg) == cfg);
    const std::string text = read_file(file);
    CHECK(contains(text, "model = /opt/m.gguf\n"));
    CHECK(contains(text, "threads = 6\n"));
    CHECK(contains(text, "hotkey = Ctrl+Shift+R\n"));
    CHECK_FALSE(contains(text, "port"));  // still the default
    // No temporary file left behind.
    CHECK(std::distance(std::filesystem::directory_iterator(tmp.path + "/a/b"), {}) == 1);
}

TEST_CASE("save_config: comments, blank lines and order are kept; only changed keys are touched") {
    TempDir tmp;
    const std::string file = tmp.path + "/config";
    const std::string original =
        "# my revisa\n\n  threads=4\nmodel = ~/m.gguf\r\n# port below\nport = 9000\n\n# end";
    write_file(file, original);
    EnvGuard home(revisa::test::kHomeVar);
    home.set("/home/ana");

    Config cfg = *parse_config(original, "x", "/home/ana").config;
    REQUIRE(cfg.model == "/home/ana/m.gguf");

    // Nothing changed: the file is not rewritten at all.
    std::string error;
    REQUIRE(save_config(file, cfg, &error));
    CHECK(read_file(file) == original);

    // port changes in place; threads goes back to automatic (its line goes); a new key is appended.
    cfg.port = 9100;
    cfg.threads = 0;
    cfg.hotkey = "Ctrl+Alt+K";
    CHECK(save_and_load(file, cfg) == cfg);
    CHECK(read_file(file) ==
          "# my revisa\n\nmodel = ~/m.gguf\r\n# port below\nport = 9100\n\n# end\r\nhotkey = Ctrl+Alt+K\r\n");

    // The model given as "~/…" or expanded is the same value: its line stays.
    cfg.model = "~/m.gguf";
    REQUIRE(save_config(file, cfg, &error));
    CHECK(contains(read_file(file), "model = ~/m.gguf\r\n"));
    cfg.model = "/srv/otro.gguf";
    cfg.hotkey = "Ctrl+Alt+Space";  // back to the default: its line is kept with the default value
    CHECK(save_and_load(file, cfg).model == "/srv/otro.gguf");
    CHECK(read_file(file) ==
          "# my revisa\n\nmodel = /srv/otro.gguf\r\n# port below\nport = 9100\n\n# end\r\nhotkey = "
          "Ctrl+Alt+Space\r\n");
}

TEST_CASE("save_config: round trip of every key through the parser") {
    using revisa::catalog::Type;
    TempDir tmp;
    const std::string file = tmp.path + "/config";
    Config cfg;
    cfg.model = "/a b/=c.gguf";
    cfg.threads = kMaxThreads;
    cfg.disabled_types = {Type::Otro, Type::ComaSobra, Type::Leismo};
    cfg.port = 1;
    cfg.hotkey = "Meta+Shift+#";
    CHECK(save_and_load(file, cfg) == cfg);
    CHECK(contains(read_file(file), "disabled_types = "));
    cfg.disabled_types.clear();
    cfg.threads = 0;
    cfg.model.clear();
    cfg.port = kDefaultPort;
    cfg.hotkey = "  Ctrl+Alt+J ";  // trimmed
    const Config back = save_and_load(file, cfg);
    CHECK(back.hotkey == "Ctrl+Alt+J");
    CHECK(read_file(file) == "port = 47312\nhotkey = Ctrl+Alt+J\n");
}

TEST_CASE("save_config: an invalid file or value leaves the file untouched") {
    TempDir tmp;
    const std::string file = tmp.path + "/config";
    const std::string bad = "# mine\nthreads = muchos\n";
    write_file(file, bad);
    std::string error;
    Config cfg;
    cfg.threads = 2;
    CHECK_FALSE(save_config(file, cfg, &error));
    CHECK(error == file + ":2: valor no válido para threads: \"muchos\" (válidos: entero de 1 a 256)");
    CHECK(read_file(file) == bad);

    write_file(file, "threads = 3\n");
    for (const auto& change : std::vector<void (*)(Config&)>{
             [](Config& c) { c.hotkey = "Ctrl+\nA"; }, [](Config& c) { c.hotkey = "   "; },
             [](Config& c) { c.threads = kMaxThreads + 1; }, [](Config& c) { c.port = 0; },
             [](Config& c) { c.model = "/x\n.gguf"; }, [](Config& c) { c.model = " /x.gguf"; }}) {
        Config c;
        change(c);
        error.clear();
        CHECK_FALSE(save_config(file, c, &error));
        CHECK_FALSE(error.empty());
        CHECK(read_file(file) == "threads = 3\n");
    }
    CHECK_FALSE(save_config(tmp.path, Config{}, &error));  // a directory
    CHECK(error == tmp.path + ": no se puede leer: es un directorio");
    Config negative;
    negative.threads = -1;
    CHECK_FALSE(save_config(file, negative, nullptr));  // error may be null
}

#ifndef _WIN32
// Creating symbolic links on Windows needs a privilege or developer mode.
TEST_CASE("save_config: a symbolic link is followed, not replaced") {
    TempDir tmp;
    const std::string real = tmp.path + "/dotfiles/config";
    const std::string link = tmp.path + "/config";
    write_file(real, "# dotfiles\n");
    std::filesystem::create_symlink(real, link);
    Config cfg;
    cfg.threads = 5;
    std::string error;
    REQUIRE(save_config(link, cfg, &error));
    CHECK(std::filesystem::is_symlink(link));
    CHECK(read_file(real) == "# dotfiles\nthreads = 5\n");
}
#endif
