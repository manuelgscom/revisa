// llama.cpp tokens of the golden prompts against the transformers ones (tests/golden/*.tokens),
// both for the whole prompt and for the prefix-cache split (prefix tokens ++ rest tokens): a BPE
// merge across the split would make the cached prefix differ from what the model was trained on.
// Needs REVISA_TEST_MODEL (a GGUF of the contract's model); skipped when it is unset.
#include "doctest.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "core/infer/prompt.hpp"
#include "llama.h"

namespace infer = revisa::infer;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot read " << path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void quiet_log(ggml_log_level, const char*, void*) {}

// As the engine tokenizes: no BOS, special tokens parsed.
std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text) {
    const auto len = static_cast<int32_t>(text.size());
    int32_t n = -llama_tokenize(vocab, text.data(), len, nullptr, 0, false, true);
    REQUIRE(n >= 0);
    std::vector<llama_token> out(static_cast<std::size_t>(n));
    n = llama_tokenize(vocab, text.data(), len, out.data(), n, false, true);
    REQUIRE(n == static_cast<int32_t>(out.size()));
    return out;
}

std::vector<llama_token> golden_tokens(const std::string& id) {
    std::vector<llama_token> ids;
    std::istringstream in(read_file(REVISA_GOLDEN_DIR "/prompt_" + id + ".tokens"));
    for (llama_token t; in >> t;) ids.push_back(t);
    return ids;
}

}  // namespace

TEST_SUITE("model") {

TEST_CASE("golden: llama.cpp tokens == transformers tokens, whole and split at the prefix") {
    const char* model_path = std::getenv("REVISA_TEST_MODEL");
    if (model_path == nullptr || *model_path == '\0') {
        MESSAGE("REVISA_TEST_MODEL not set: skipped");
        return;
    }
    llama_log_set(quiet_log, nullptr);
    llama_backend_init();
    llama_model_params params = llama_model_default_params();
    params.vocab_only = true;
    llama_model* model = llama_model_load_from_file(model_path, params);
    REQUIRE_MESSAGE(model != nullptr, "cannot load " << model_path);
    const llama_vocab* vocab = llama_model_get_vocab(model);

    CHECK(tokenize(vocab, "<|im_end|>").size() == 1);

    const std::vector<llama_token> prefix = tokenize(vocab, infer::prompt_prefix());
    MESSAGE("prompt prefix: " << prefix.size() << " tokens");
    for (const auto& c : nlohmann::json::parse(read_file(REVISA_GOLDEN_DIR "/cases.json"))) {
        const std::string id = c.at("id").get<std::string>();
        const std::string segment = c.at("segment").get<std::string>();
        CAPTURE(id);
        const std::vector<llama_token> want = golden_tokens(id);
        REQUIRE(!want.empty());
        CHECK(tokenize(vocab, read_file(REVISA_GOLDEN_DIR "/prompt_" + id + ".txt")) == want);

        std::vector<llama_token> split = prefix;
        const std::vector<llama_token> rest = tokenize(vocab, infer::prompt_rest(segment));
        split.insert(split.end(), rest.begin(), rest.end());
        CHECK(split == want);
    }
    llama_model_free(model);
    llama_backend_free();
}

}  // TEST_SUITE
