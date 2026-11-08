// check_text fills Result::timings: one entry per segment sent to the engine, in order.
#include "doctest.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "core/check/check.hpp"
#include "core/infer/fake_engine.hpp"
#include "test_util.hpp"

#if REVISA_TEST_HOOKS

namespace {

// Sets REVISA_FAKE_MAP to a temporary map file (the other REVISA_FAKE_* unset) and restores the
// environment and removes the file on destruction.
class FakeMap {
public:
    explicit FakeMap(const std::string& json) {
        path_ = (std::filesystem::temp_directory_path() /
                 ("revisa_timings_map_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".json"))
                    .string();
        std::ofstream(path_, std::ios::binary) << json;
        for (int i = 0; i < 4; ++i) {
            if (const char* v = std::getenv(kVars[i])) saved_[i] = v;
            revisa::test::unset_env(kVars[i]);
        }
        revisa::test::set_env("REVISA_FAKE_MAP", path_);
    }
    ~FakeMap() {
        for (int i = 0; i < 4; ++i) {
            if (!saved_[i].empty()) revisa::test::set_env(kVars[i], saved_[i]);
            else revisa::test::unset_env(kVars[i]);
        }
        std::remove(path_.c_str());
    }
    FakeMap(const FakeMap&) = delete;
    FakeMap& operator=(const FakeMap&) = delete;

private:
    static constexpr const char* kVars[4] = {"REVISA_FAKE_ERROR", "REVISA_FAKE_ERROR_DETAIL", "REVISA_FAKE_MAP",
                                             "REVISA_FAKE_OUTPUT"};
    std::string path_;
    std::string saved_[4];
};

}  // namespace

TEST_CASE("check_text: one timing per segment sent to the engine, changed only if the output differs") {
    const FakeMap map(R"({"Ya e echo el informe.": "Ya he hecho el informe."})");
    const std::unique_ptr<revisa::infer::Engine> engine = revisa::infer::fake_engine_from_env();
    REQUIRE(engine != nullptr);

    // The URL line is protected: it never reaches the engine.
    const std::string text = "Ya e echo el informe. Esto está bien.\nhttps://example.org/a/b\nOtra frase sin cambios.";
    const revisa::check::Result r = revisa::check::check_text(text, *engine);
    REQUIRE(r.error.empty());
    CHECK(r.changed);
    REQUIRE(r.timings.size() == 3);
    CHECK(r.timings[0].changed);
    CHECK_FALSE(r.timings[1].changed);
    CHECK_FALSE(r.timings[2].changed);
    for (const auto& t : r.timings) CHECK(t.ms >= 0);

    // No segment for the model: no timings.
    CHECK(revisa::check::check_text("https://example.org/a/b", *engine).timings.empty());
}

#endif  // REVISA_TEST_HOOKS
