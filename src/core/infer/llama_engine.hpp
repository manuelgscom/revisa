#pragma once
// Engine backed by llama.cpp on the CPU, with the model loaded lazily on the first correct().
//
// Contract of the engine:
//   n_ctx kCtx, greedy, stop at <|im_end|> (or any end-of-generation token), at most
//   max_new_tokens(segment tokens) generated tokens (cap_reached otherwise);
//   tokenization with parse_special = true and no BOS;
//   prefix state in memory: after loading, prompt_prefix() is decoded once and its sequence state
//   saved (llama_state_seq_get_data). Every call clears the memory, restores that state and
//   decodes prompt_rest(segment). The prompt is therefore always decoded in the same two chunks,
//   so the output does not depend on the cache.
//
// Two generation paths, both greedy (argmax, ties to the lowest id):
//   speculative = false: one llama_decode per generated token. The reference output.
//   speculative = true (default): input-guided speculative decoding. The draft is
//     the input segment from the sync point on (prompt lookup of the last 3, then 2, emitted
//     tokens), verified in one batch with logits on every position; the longest matching prefix
//     plus the first differing argmax are emitted. Before each batch the recurrent state is
//     saved (PARTIAL_ONLY) and restored on a rejection, then only the accepted tokens are
//     decoded again. Same output as the reference except for numerical near-ties between
//     batched and one-token logits.

#include <memory>
#include <string>

#include "core/infer/engine.hpp"

namespace revisa::infer {

inline constexpr int kCtx = 1024;
inline constexpr int kBatch = 1024;
inline constexpr int kUbatch = 512;

struct LlamaOptions {
    std::string model_path;
    int threads = 0;           // 0 = default_threads()
    bool speculative = true;   // input-guided speculative decoding; false = --no-spec
    bool debug = false;        // llama.cpp/ggml logs to stderr (REVISA_DEBUG=1)
};

std::unique_ptr<Engine> make_llama_engine(const LlamaOptions& options);

}  // namespace revisa::infer
