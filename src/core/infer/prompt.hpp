#pragma once
// Prompt of the model contract. The code is the source of truth
// for the binary; tests/golden (generated with transformers) and contrato_doc_test check it.
// Any change bumps kContractVersion and regenerates tests/golden.

#include <cstddef>
#include <string>
#include <string_view>

namespace revisa::infer {

// Longest segment the model sees, in Unicode code points. Longer ones are not corrected.
inline constexpr std::size_t kMaxSegmentChars = 400;

// System prompt of contract v1 (no examples).
extern const std::string_view kSystemPrompt;

// Equivalent to Python's str.strip(): removes leading and trailing Unicode whitespace as defined
// by str.isspace (what the chat template's |trim does). Invalid UTF-8 is kept as is.
std::string_view trim_like_python(std::string_view text);

// The exact contract string for one segment (trimmed here):
//   "<|im_start|>system\n{kSystemPrompt}<|im_end|>\n<|im_start|>user\n{segment}<|im_end|>\n"
//   "<|im_start|>assistant\n<think>\n\n</think>\n\n"
// Equivalent to apply_chat_template([system, user], tokenize=False, add_generation_prompt=True,
// enable_thinking=False).
std::string render_qwen35(std::string_view segment);

// Cacheable prefix: everything up to and including "<|im_start|>user\n".
std::string prompt_prefix();

// The rest after the prefix. prompt_prefix() + prompt_rest(s) == render_qwen35(s).
std::string prompt_rest(std::string_view segment);

// Generation cap for a segment of n tokens: ceil(1.5 * n) + 8.
constexpr int max_new_tokens(int n) { return (3 * n + 1) / 2 + 8; }

}  // namespace revisa::infer
