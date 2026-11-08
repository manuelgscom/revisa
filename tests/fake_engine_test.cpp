#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include "core/infer/fake_engine.hpp"
#include "doctest.h"
#include "test_util.hpp"

#if REVISA_TEST_HOOKS

using namespace revisa::infer;
using revisa::test::set_env;
using revisa::test::unset_env;

namespace {

constexpr const char* kVars[] = {"REVISA_FAKE_ERROR", "REVISA_FAKE_ERROR_DETAIL", "REVISA_FAKE_MAP",
                                 "REVISA_FAKE_OUTPUT"};

// Clears the REVISA_FAKE_* variables and restores their previous values on destruction.
class EnvGuard {
public:
    EnvGuard() {
        for (std::size_t i = 0; i < std::size(kVars); ++i) {
            if (const char* v = std::getenv(kVars[i])) saved_[i] = v;
            unset_env(kVars[i]);
        }
    }
    ~EnvGuard() {
        for (std::size_t i = 0; i < std::size(kVars); ++i) {
            if (saved_[i]) {
                set_env(kVars[i], *saved_[i]);
            } else {
                unset_env(kVars[i]);
            }
        }
    }
    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

private:
    std::optional<std::string> saved_[std::size(kVars)];
};

// Temporary file removed on destruction.
struct TempFile {
    explicit TempFile(const std::string& content) {
        path = (std::filesystem::temp_directory_path() /
                ("revisa_fake_map_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".json"))
                   .string();
        std::ofstream(path, std::ios::binary) << content;
    }
    ~TempFile() { std::remove(path.c_str()); }
    std::string path;
};

}  // namespace

TEST_CASE("fake engine: nullptr when nothing is set") {
    EnvGuard env;
    CHECK(fake_engine_from_env() == nullptr);
}

TEST_CASE("fake engine: REVISA_FAKE_OUTPUT") {
    EnvGuard env;
    set_env("REVISA_FAKE_OUTPUT", "A ver si mañana podemos vernos.");
    auto engine = fake_engine_from_env();
    REQUIRE(engine != nullptr);
    const EngineResult r = engine->correct("haber si mañana podemos vernos.");
    REQUIRE(r.text);
    CHECK(*r.text == "A ver si mañana podemos vernos.");
    CHECK(r.error.empty());
}

TEST_CASE("fake engine: REVISA_FAKE_ERROR wins over the others") {
    EnvGuard env;
    set_env("REVISA_FAKE_ERROR", "cap_reached");
    set_env("REVISA_FAKE_ERROR_DETAIL", "detail");
    set_env("REVISA_FAKE_OUTPUT", "x");
    set_env("REVISA_FAKE_MAP", "/nonexistent/map.json");
    auto engine = fake_engine_from_env();
    REQUIRE(engine != nullptr);
    const EngineResult r = engine->correct("hola");
    CHECK_FALSE(r.text);
    CHECK(r.error == "cap_reached");
    CHECK(r.error_detail == "detail");
}

TEST_CASE("fake engine: REVISA_FAKE_MAP hit and miss, and wins over REVISA_FAKE_OUTPUT") {
    EnvGuard env;
    TempFile map(R"({"haber si vienes": "a ver si vienes", "que tal": "¿Qué tal?"})");
    set_env("REVISA_FAKE_MAP", map.path);
    set_env("REVISA_FAKE_OUTPUT", "ignored");
    auto engine = fake_engine_from_env();
    REQUIRE(engine != nullptr);

    EngineResult r = engine->correct("  haber si vienes\n");  // the key is the trimmed segment
    REQUIRE(r.text);
    CHECK(*r.text == "a ver si vienes");
    r = engine->correct("que tal");
    REQUIRE(r.text);
    CHECK(*r.text == "¿Qué tal?");
    r = engine->correct("Nos vemos.");
    REQUIRE(r.text);
    CHECK(*r.text == "Nos vemos.");
    CHECK(r.error.empty());
}

TEST_CASE("fake engine: invalid or unreadable REVISA_FAKE_MAP gives load_failed") {
    EnvGuard env;
    TempFile bad_json("{not json");
    TempFile not_object(R"(["a", "b"])");
    TempFile not_strings(R"({"a": 1})");
    for (const std::string& path :
         {bad_json.path, not_object.path, not_strings.path, std::string("/nonexistent/map.json")}) {
        CAPTURE(path);
        set_env("REVISA_FAKE_MAP", path);
        auto engine = fake_engine_from_env();
        REQUIRE(engine != nullptr);
        for (int i = 0; i < 2; ++i) {
            const EngineResult r = engine->correct("a");
            CHECK_FALSE(r.text);
            CHECK(r.error == "load_failed");
            CHECK_FALSE(r.error_detail.empty());
        }
    }
}

#endif
