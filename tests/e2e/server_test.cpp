// revisa serve end to end: the real binary in the background with the fake engine (REVISA_FAKE_MAP)
// in a sandboxed HOME, talked to over HTTP with cpp-httplib's client.

#include "doctest.h"

#include <stdlib.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "pty_session.hpp"
#include "server_proc.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using revisa::e2e::BackgroundProcess;

namespace {

constexpr int kTimeoutMs = 20000;

// Temporary HOME and XDG dirs; nothing of the runner's environment is inherited.
struct ServeSandbox {
    std::string dir;
    std::map<std::string, std::string> vars;

    ServeSandbox() {
        std::string tmpl = (fs::temp_directory_path() / "revisa-e2e-serve-XXXXXX").string();
        REQUIRE(mkdtemp(tmpl.data()) != nullptr);
        dir = tmpl;
        vars = {
            {"HOME", dir},
            {"XDG_CONFIG_HOME", dir + "/.config"},
            {"XDG_CACHE_HOME", dir + "/.cache"},
            {"XDG_DATA_HOME", dir + "/.local/share"},
            {"TMPDIR", dir},
            {"NO_COLOR", "1"},
            {"LANG", "es_ES.UTF-8"},
            {"PATH", "/usr/bin:/bin"},
        };
    }
    ~ServeSandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    ServeSandbox(const ServeSandbox&) = delete;
    ServeSandbox& operator=(const ServeSandbox&) = delete;

    std::string path(const std::string& name) const { return dir + "/" + name; }

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
    revisa::e2e::PipeResult pipes(const std::vector<std::string>& args) const {
        return revisa::e2e::run_with_pipes(argv(args), env(), "", kTimeoutMs);
    }
    std::string read(const std::string& name) const {
        std::ifstream f(path(name), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
};

httplib::Headers bearer(const std::string& token) { return {{"Authorization", "Bearer " + token}}; }

}  // namespace

TEST_CASE("e2e serve: tokens, security checks, limits and UTF-16 offsets") {
    ServeSandbox sb;
    // One segment with an emoji (two UTF-16 units, four bytes) before the error.
    const std::string wrong = "😀 Te boy a llamar.";
    const std::string right = "😀 Te voy a llamar.";
    {
        std::ofstream(sb.path("fake-map.json")) << json{{wrong, right}}.dump();
    }
    sb.vars["REVISA_FAKE_MAP"] = sb.path("fake-map.json");

    // revisa token: new prints only the token; list never prints it.
    auto r = sb.pipes({"token", "list"});
    CHECK(r.status.exited);
    CHECK(r.status.code == 0);
    CHECK(r.out.find("No hay tokens") != std::string::npos);
    r = sb.pipes({"token", "new", "e2e"});
    REQUIRE(r.status.code == 0);
    const std::string token = r.out.substr(0, r.out.find('\n'));
    REQUIRE(token.size() == 64);
    CHECK(r.out == token + "\n");
    CHECK(sb.pipes({"token", "new", "e2e"}).status.code == 2);
    CHECK(sb.pipes({"token", "new", "con espacio"}).status.code == 2);
    r = sb.pipes({"token", "list"});
    CHECK(r.status.code == 0);
    CHECK(r.out.find("e2e  creado ") == 0);
    CHECK(r.out.find(token) == std::string::npos);

    const int port = revisa::e2e::free_port();
    REQUIRE(port > 0);
    BackgroundProcess serve(sb.argv({"serve", "--port", std::to_string(port)}), sb.env(), sb.path("serve.log"));
    REQUIRE(serve.started());

    httplib::Client c("127.0.0.1", port);
    c.set_connection_timeout(std::chrono::seconds(1));
    c.set_read_timeout(std::chrono::seconds(10));
    bool ready = false;
    for (int i = 0; i < 200 && !ready && serve.alive(); ++i) {
        const auto res = c.Get("/v1/health");
        if (res && res->status == 200 && json::parse(res->body)["ready"] == true) ready = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE_MESSAGE(ready, sb.read("serve.log"));
    CHECK(sb.read("serve.log").find("escuchando en http://127.0.0.1:" + std::to_string(port)) != std::string::npos);

    const std::string body = json{{"text", wrong}}.dump();

    auto res = c.Post("/v1/check", body, "application/json");
    REQUIRE(res);
    CHECK(res->status == 401);

    res = c.Post("/v1/check", httplib::Headers{{"Authorization", "Bearer " + token}, {"Origin", "https://example.com"}},
                 body, "application/json");
    REQUIRE(res);
    CHECK(res->status == 403);

    res = c.Post("/v1/check", httplib::Headers{{"Authorization", "Bearer " + token}, {"Host", "evil.example"}}, body,
                 "application/json");
    REQUIRE(res);
    CHECK(res->status == 403);

    res = c.Post("/v1/check", bearer(token), json{{"text", std::string(20001, 'a')}}.dump(), "application/json");
    REQUIRE(res);
    CHECK(res->status == 413);

    res = c.Post("/v1/pair", R"({"name": "e2e"})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 403);

    res = c.Post("/v1/check", httplib::Headers{{"Authorization", "Bearer " + token}, {"Origin", "chrome-extension://abc"}},
                 json{{"text", wrong}, {"field", "f"}}.dump(), "application/json");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    const json j = json::parse(res->body);
    CHECK(j["corrected"] == right);
    REQUIRE(j["edits"].size() == 1);
    CHECK(j["edits"][0]["start"] == 6);  // 😀 = 2 UTF-16 units, then " Te "
    CHECK(j["edits"][0]["end"] == 9);
    CHECK(j["edits"][0]["from"] == "boy");
    CHECK(j["edits"][0]["to"] == "voy");

    // Revocation applies to the running server at once.
    CHECK(sb.pipes({"token", "revoke", "e2e"}).status.code == 0);
    CHECK(sb.pipes({"token", "revoke", "e2e"}).status.code == 2);
    res = c.Post("/v1/check", bearer(token), body, "application/json");
    REQUIRE(res);
    CHECK(res->status == 401);

    // A second server cannot take the same port.
    r = sb.pipes({"serve", "--port", std::to_string(port)});
    CHECK(r.status.code == 2);
    CHECK(r.err.find("no se puede escuchar") != std::string::npos);

    // SIGTERM is a clean stop.
    REQUIRE(serve.stop(SIGTERM, kTimeoutMs));
    CHECK(serve.exit_code() == 0);
    // The user's text never reaches the log.
    CHECK(sb.read("serve.log").find("boy") == std::string::npos);
}
