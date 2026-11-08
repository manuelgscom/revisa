#include <cstdlib>
#include <string>

#include "core/infer/llama_engine.hpp"
#include "core/infer/prompt.hpp"
#include "doctest.h"

using namespace revisa::infer;

namespace {

// Model of the "model" suite; empty => the test is skipped.
std::string test_model() {
    const char* v = std::getenv("REVISA_TEST_MODEL");
    return v != nullptr ? v : "";
}

}  // namespace

TEST_SUITE("model") {
    TEST_CASE("llama engine: corrects a sentence, deterministically") {
        const std::string model = test_model();
        if (model.empty()) {
            MESSAGE("REVISA_TEST_MODEL not set: skipped");
            return;
        }
        auto engine = make_llama_engine({.model_path = model});
        const char* sentence = "haber si mañana podemos vernos";
        const EngineResult first = engine->correct(sentence);
        REQUIRE(first.text);
        CHECK(first.error.empty());
        CHECK_FALSE(first.text->empty());
        CHECK(first.timings.gen_tokens < max_new_tokens(first.timings.segment_tokens));
        CHECK(first.timings.load_ms > 0);
        MESSAGE("output: «" << *first.text << "» load " << first.timings.load_ms << " ms, total "
                            << first.timings.total_ms << " ms");

        const EngineResult second = engine->correct(sentence);
        REQUIRE(second.text);
        CHECK(*second.text == *first.text);
        CHECK(second.timings.load_ms == 0);
        MESSAGE("second call: prefill " << second.timings.prefill_ms << " ms, generate "
                                        << second.timings.generate_ms << " ms, total " << second.timings.total_ms
                                        << " ms, " << second.timings.gen_tokens << " tokens");

        auto fresh = make_llama_engine({.model_path = model});
        const EngineResult third = fresh->correct(sentence);
        REQUIRE(third.text);
        CHECK(*third.text == *first.text);
    }

    TEST_CASE("llama engine: a correct sentence") {
        const std::string model = test_model();
        if (model.empty()) {
            MESSAGE("REVISA_TEST_MODEL not set: skipped");
            return;
        }
        auto engine = make_llama_engine({.model_path = model});
        const EngineResult r = engine->correct("Nos vemos mañana a las cinco.");
        REQUIRE(r.text);
        CHECK(r.error.empty());
        MESSAGE("output: «" << *r.text << "» total " << r.timings.total_ms << " ms");
    }

    TEST_CASE("llama engine: too_long") {
        const std::string model = test_model();
        if (model.empty()) {
            MESSAGE("REVISA_TEST_MODEL not set: skipped");
            return;
        }
        std::string segment;
        for (int i = 0; i < 3000; ++i) segment += "a ";
        auto engine = make_llama_engine({.model_path = model});
        const EngineResult r = engine->correct(segment);
        CHECK_FALSE(r.text);
        CHECK(r.error == "too_long");
        CHECK(r.timings.segment_tokens > 0);
    }

    TEST_CASE("llama engine: load_failed is cached") {
        auto engine = make_llama_engine({.model_path = "/nonexistent/model.gguf"});
        const EngineResult first = engine->correct("hola");
        CHECK_FALSE(first.text);
        CHECK(first.error == "load_failed");
        CHECK_FALSE(first.error_detail.empty());
        const EngineResult second = engine->correct("hola");
        CHECK_FALSE(second.text);
        CHECK(second.error == "load_failed");
        CHECK(second.error_detail == first.error_detail);
    }
}
