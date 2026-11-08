#pragma once
// Model engine interface: one segment in, the corrected segment out (model contract v1).

#include <optional>
#include <string>
#include <string_view>

namespace revisa::infer {

// Version of the model contract. Any change to the system prompt, the
// segment normalization or the output format bumps it and regenerates tests/golden.
inline constexpr int kContractVersion = 1;

struct Timings {
    double load_ms = 0;      // model load + prefix decode (first call only)
    double prefill_ms = 0;   // restore of the prefix state + decode of the rest of the prompt
    double generate_ms = 0;
    double total_ms = 0;
    int prompt_tokens = 0;   // whole prompt (prefix + rest)
    int segment_tokens = 0;  // the trimmed segment alone: the n of max_new_tokens(n)
    int gen_tokens = 0;      // generated tokens, without <|im_end|>
};

// Error codes (stable, untranslated):
//   cap_reached    no <|im_end|> within max_new_tokens(n): the output is discarded
//   too_long       prompt + max_new_tokens(n) does not fit in the context
//   load_failed    the model could not be loaded (the first failure is cached)
//   decode_failed  llama_decode or the state restore failed
// error_detail: human-readable, untranslated.
struct EngineResult {
    std::optional<std::string> text;  // generated text before <|im_end|>, verbatim; empty => error
    std::string error;
    std::string error_detail;
    Timings timings;
};

class Engine {
public:
    virtual ~Engine() = default;
    // segment: one segment of the user's text. It is trimmed (trim_like_python) before rendering,
    // as the chat template does.
    virtual EngineResult correct(std::string_view segment) = 0;
};

}  // namespace revisa::infer
