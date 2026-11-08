#include "doctest.h"

#include "server/server.hpp"
#include "test_util.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "core/check/check.hpp"
#include "core/tokens/tokens.hpp"

using namespace revisa;
namespace fs = std::filesystem;
using json = nlohmann::json;

TEST_CASE("server: Host must be the loopback address with our port") {
    CHECK(server::host_allowed("127.0.0.1:47312", 47312));
    CHECK(server::host_allowed("localhost:47312", 47312));
    CHECK_FALSE(server::host_allowed("127.0.0.1:47313", 47312));
    CHECK_FALSE(server::host_allowed("127.0.0.1", 47312));
    CHECK_FALSE(server::host_allowed("localhost", 47312));
    CHECK_FALSE(server::host_allowed("evil.example:47312", 47312));
    CHECK_FALSE(server::host_allowed("evil.example", 47312));
    CHECK_FALSE(server::host_allowed("127.0.0.1.evil.example:47312", 47312));
    CHECK_FALSE(server::host_allowed("[::1]:47312", 47312));
    CHECK_FALSE(server::host_allowed("", 47312));
}

TEST_CASE("server: Origin allows only browser extensions or nothing") {
    CHECK(server::origin_allowed(std::nullopt));
    CHECK(server::origin_allowed("chrome-extension://abcdefghijklmnop"));
    CHECK(server::origin_allowed("moz-extension://1234-5678"));
    CHECK_FALSE(server::origin_allowed("https://example.com"));
    CHECK_FALSE(server::origin_allowed("http://127.0.0.1:47312"));
    CHECK_FALSE(server::origin_allowed("null"));
    CHECK_FALSE(server::origin_allowed(""));
    CHECK_FALSE(server::origin_allowed("file://"));
    CHECK_FALSE(server::origin_allowed("https://chrome-extension://x"));
}

TEST_CASE("server: warm-up text reaches the model") { CHECK(check::needs_model("Hola.")); }

namespace {

// Corrects the segments of a map; "Espera." blocks until released.
class TestEngine final : public infer::Engine {
public:
    infer::EngineResult correct(std::string_view segment) override {
        if (segment == "Espera.") {
            std::unique_lock lock(mutex_);
            blocked_ = true;
            cv_.notify_all();
            cv_.wait(lock, [this] { return released_; });
        }
        infer::EngineResult r;
        const auto it = map_.find(std::string(segment));
        r.text = it != map_.end() ? it->second : std::string(segment);
        return r;
    }
    void wait_blocked() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return blocked_; });
    }
    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }

private:
    std::map<std::string, std::string> map_ = {{"Te boy a llamar.", "Te voy a llamar."}};
    std::mutex mutex_;
    std::condition_variable cv_;
    bool blocked_ = false;
    bool released_ = false;
};

struct TempDir {
    std::string dir;
    TempDir() : dir(revisa::test::make_temp_dir("revisa-server-")) {}
    ~TempDir() { revisa::test::remove_tree(dir); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// A server on a free port, running on a thread of its own.
struct Running {
    TestEngine engine;
    server::Server srv;
    std::thread thread;
    Running(server::Options o) : srv(engine, std::move(o)) {
        REQUIRE(srv.bind());
        thread = std::thread([this] { srv.run(); });
    }
    ~Running() {
        engine.release();
        srv.stop();
        thread.join();
    }
    Running(const Running&) = delete;
    Running& operator=(const Running&) = delete;

    httplib::Client client() const {
        httplib::Client c("127.0.0.1", srv.port());
        c.set_read_timeout(std::chrono::seconds(10));
        return c;
    }
};

httplib::Headers bearer(const std::string& token) { return {{"Authorization", "Bearer " + token}}; }

}  // namespace

TEST_CASE("server: in-process requests") {
    TempDir tmp;
    const std::string tokens_path = tmp.dir + "/tokens";
    const tokens::Change t = tokens::add(tokens_path, "test");
    REQUIRE(t.status == tokens::Status::Ok);

    server::Options o;
    o.model_name = "test-model";
    o.tokens_path = tokens_path;
    Running r(o);
    httplib::Client c = r.client();

    for (int i = 0; i < 200 && !r.srv.ready(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto res = c.Get("/v1/health");
    REQUIRE(res);
    CHECK(res->status == 200);
    const json health = json::parse(res->body);
    CHECK(health["model"] == "test-model");
    CHECK(health["ready"] == true);

    res = c.Get("/v1/health", httplib::Headers{{"Origin", "https://example.com"}});
    REQUIRE(res);
    CHECK(res->status == 403);
    res = c.Get("/v1/health", httplib::Headers{{"Origin", "moz-extension://x"}});
    REQUIRE(res);
    CHECK(res->status == 200);
    res = c.Get("/v1/health", httplib::Headers{{"Host", "evil.example:" + std::to_string(r.srv.port())}});
    REQUIRE(res);
    CHECK(res->status == 403);

    const std::string body = R"({"text": "Hola. Te boy a llamar."})";
    res = c.Post("/v1/check", body, "application/json");
    REQUIRE(res);
    CHECK(res->status == 401);
    res = c.Post("/v1/check", bearer("wrong"), body, "application/json");
    REQUIRE(res);
    CHECK(res->status == 401);

    res = c.Post("/v1/check", bearer(t.token), body, "application/json");
    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->get_header_value("Content-Type") == "application/json");
    CHECK_FALSE(res->has_header("Access-Control-Allow-Origin"));
    json j = json::parse(res->body);
    CHECK(j["model"] == "test-model");
    CHECK(j["corrected"] == "Hola. Te voy a llamar.");
    REQUIRE(j["edits"].size() == 1);
    CHECK(j["edits"][0]["start"] == 9);
    CHECK(j["edits"][0]["type"] == "B_V");

    res = c.Post("/v1/check", bearer(t.token), R"({"text": "Hola. Te boy a llamar.", "disabled_types": ["B_V"]})",
                 "application/json");
    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(json::parse(res->body)["corrected"] == "Hola. Te boy a llamar.");

    for (const char* bad : {"{", "[]", R"({"text": 1})", R"({"txt": "a"})", R"({"text": "a", "disabled_types": "B_V"})",
                            R"({"text": "a", "disabled_types": [1]})", R"({"text": "a", "field": 2})"}) {
        res = c.Post("/v1/check", bearer(t.token), bad, "application/json");
        REQUIRE(res);
        CHECK_MESSAGE(res->status == 400, bad);
    }
    res = c.Post("/v1/check", bearer(t.token), R"({"text": "a", "disabled_types": ["NO_EXISTE"]})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 400);
    CHECK(json::parse(res->body)["error"] == "unknown_type");

    // 20 000 code points pass the limit check; one more is 413 (also counted in code points).
    std::string ok_text;
    for (std::size_t i = 0; i < server::kMaxTextChars; ++i) ok_text += "ñ";
    res = c.Post("/v1/check", bearer(t.token), json{{"text", ok_text}}.dump(), "application/json");
    REQUIRE(res);
    CHECK(res->status == 200);
    res = c.Post("/v1/check", bearer(t.token), json{{"text", ok_text + "ñ"}}.dump(), "application/json");
    REQUIRE(res);
    CHECK(res->status == 413);
    res = c.Post("/v1/check", bearer(t.token), std::string(server::kMaxBodyBytes + 1, ' '), "application/json");
    REQUIRE(res);
    CHECK(res->status == 413);

    res = c.Post("/v1/pair", R"({"name": "x"})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 403);
    CHECK(json::parse(res->body)["error"] == "pairing_disabled");

    // Revocation is immediate.
    REQUIRE(tokens::revoke(tokens_path, "test").status == tokens::Status::Ok);
    res = c.Post("/v1/check", bearer(t.token), body, "application/json");
    REQUIRE(res);
    CHECK(res->status == 401);
}

TEST_CASE("server: a newer check of the same field supersedes a pending one") {
    TempDir tmp;
    const std::string tokens_path = tmp.dir + "/tokens";
    const tokens::Change t = tokens::add(tokens_path, "test");
    REQUIRE(t.status == tokens::Status::Ok);
    server::Options o;
    o.tokens_path = tokens_path;
    Running r(o);

    const auto post = [&](const std::string& body) {
        httplib::Client c = r.client();
        auto res = c.Post("/v1/check", bearer(t.token), body, "application/json");
        return res ? res->status : -1;
    };
    // Occupies the inference thread.
    std::thread blocker([&] { CHECK(post(R"({"text": "Espera."})") == 200); });
    r.engine.wait_blocked();
    std::atomic<int> a{0}, b{0};
    std::thread ta([&] { a = post(R"({"text": "Uno.", "field": "f"})"); });
    std::thread tb([&] { b = post(R"({"text": "Dos.", "field": "f"})"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // both queued behind the blocker
    r.engine.release();
    blocker.join();
    ta.join();
    tb.join();
    // Whichever arrived first was superseded by the other.
    CHECK(((a == 409 && b == 200) || (a == 200 && b == 409)));
}

TEST_CASE("server: pairing with approval") {
    TempDir tmp;
    const std::string tokens_path = tmp.dir + "/tokens";
    server::Options o;
    o.tokens_path = tokens_path;
    std::string asked;
    o.approve_pair = [&](const std::string& name) {
        asked = name;
        return name != "Malo";
    };
    Running r(o);
    httplib::Client c = r.client();

    auto res = c.Post("/v1/pair", R"({"name": "Firefox en casa"})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(asked == "Firefox-en-casa");
    const std::string token = json::parse(res->body)["token"];
    const tokens::LoadResult loaded = tokens::load(tokens_path);
    REQUIRE(loaded.entries);
    CHECK(tokens::verify(*loaded.entries, token));

    res = c.Post("/v1/check", bearer(token), R"({"text": "Hola."})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 200);

    res = c.Post("/v1/pair", R"({"name": "Malo"})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 403);
    res = c.Post("/v1/pair", R"({"nombre": "x"})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 400);
}

TEST_CASE("server: a taken port cannot be bound") {
    TestEngine engine;
    server::Server a(engine, {});
    REQUIRE(a.bind());
    server::Options o;
    o.port = a.port();
    server::Server b(engine, o);
    CHECK_FALSE(b.bind());
}
