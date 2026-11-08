// Input-guided speculative decoding against plain greedy decoding: the same outputs.
// Needs REVISA_TEST_MODEL; skipped when it is unset.
#include "doctest.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/infer/cpu.hpp"
#include "core/infer/llama_engine.hpp"

using namespace revisa::infer;

namespace {

std::string test_model() {
    const char* v = std::getenv("REVISA_TEST_MODEL");
    return v != nullptr ? v : "";
}

std::vector<std::string> golden_segments() {
    std::ifstream in(REVISA_GOLDEN_DIR "/cases.json", std::ios::binary);
    REQUIRE(in.good());
    std::vector<std::string> out;
    for (const auto& c : nlohmann::json::parse(in)) out.push_back(c.at("segment").get<std::string>());
    return out;
}

}  // namespace

TEST_CASE("cpu: default_threads is between 1 and the physical cores") {
    const int n = default_threads();
    CHECK(n >= 1);
    CHECK(n <= physical_cores());
}

TEST_SUITE("model") {
    TEST_CASE("spec: speculative and greedy decoding give the same outputs") {
        const std::string model = test_model();
        if (model.empty()) {
            MESSAGE("REVISA_TEST_MODEL not set: skipped");
            return;
        }
        auto spec = make_llama_engine({.model_path = model, .speculative = true});
        auto greedy = make_llama_engine({.model_path = model, .speculative = false});

        std::vector<std::string> segments = golden_segments();
        segments.insert(segments.end(), {"Ya e echo el informe.", "Haber si vienes mañana.",
                                         "Nos vemos mañana a las cinco en la puerta del cine."});
        // Batched verification and token-by-token decoding may differ on numerical near-ties
        // (target: >= 99 % identical on 500 dev cases). With GCC the golden set is exact; MSVC's
        // ggml build hits one near-tie in it (empieza_espacio_tras_salto: one indentation space,
        // which the guards then drop from the product's output), so there one discordant segment
        // is tolerated and reported.
#ifdef _MSC_VER
        constexpr int kMaxDiscordant = 1;
#else
        constexpr int kMaxDiscordant = 0;
#endif
        int discordant = 0;
        double ms_spec = 0, ms_greedy = 0;
        for (const std::string& s : segments) {
            CAPTURE(s);
            const EngineResult a = spec->correct(s);
            const EngineResult b = greedy->correct(s);
            CHECK(a.error == b.error);
            CHECK(a.text.has_value() == b.text.has_value());
            if (a.text && b.text && *a.text != *b.text) {
                ++discordant;
                MESSAGE("speculative differs from greedy: [" << *a.text << "] vs [" << *b.text << "]");
            } else {
                CHECK(a.timings.gen_tokens == b.timings.gen_tokens);
            }
            ms_spec += a.timings.generate_ms;
            ms_greedy += b.timings.generate_ms;
        }
        CHECK(discordant <= kMaxDiscordant);
        MESSAGE(segments.size() << " segments, generation: speculative " << ms_spec << " ms, greedy " << ms_greedy
                                << " ms");
    }
}
