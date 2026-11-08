#pragma once
// The model revisa downloads. The single place that holds it.
// revisa-0.8b in Q4_K_M (fine-tuned from Qwen3.5-0.8B; GGUF with the published metadata),
// checked by size and SHA-256.

#include <cstdint>

namespace revisa::fetch {

struct ModelInfo {
    const char* name;      // shown in --json "model"
    const char* filename;  // file name inside models_dir()
    const char* url;
    std::uint64_t size;
    const char* sha256;
};

// -DREVISA_MODEL_URL=... overrides the URL at build time (CMakeLists.txt).
inline constexpr ModelInfo kDefaultModel = {
    "revisa-0.8b",
    "revisa-0.8b-Q4_K_M.gguf",
#ifdef REVISA_MODEL_URL
    REVISA_MODEL_URL,
#else
    // Pinned to the Hugging Face commit that published this file: later pushes to the
    // repository (card edits) never change what a given binary downloads.
    "https://huggingface.co/manuelgscom/revisa-0.8b-GGUF/resolve/"
    "9e753cc03f4be703f956b672b63d301fa6925af5/revisa-0.8b-Q4_K_M.gguf",
#endif
    529297888ULL,
    "7ee833111c7574aa5b37eed9b292ab21c539b203d687250f8e8deee0cba9127b",
};

}  // namespace revisa::fetch
