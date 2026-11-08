#include "core/infer/prompt.hpp"

#include <cstdint>

namespace revisa::infer {

// System prompt of contract v1, no examples. Any change bumps kContractVersion and regenerates
// tests/golden.
const std::string_view kSystemPrompt =
    "Corrige los errores de ortografía y gramática del texto según la norma del español. "
    "No cambies el estilo, el vocabulario ni el registro. Si el texto es correcto, devuélvelo igual. "
    "Responde solo con el texto.";

namespace {

constexpr std::string_view kSystemHeader = "<|im_start|>system\n";
constexpr std::string_view kUserHeader = "<|im_start|>user\n";
constexpr std::string_view kTurnEnd = "<|im_end|>\n";
constexpr std::string_view kAssistantHeader = "<|im_start|>assistant\n<think>\n\n</think>\n\n";

// Decodes the UTF-8 code point at s[i]. An invalid sequence counts as a single non-space byte.
std::uint32_t decode(std::string_view s, std::size_t i, std::size_t& len) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    len = 1;
    if (b0 < 0x80) return b0;
    std::size_t n = 0;
    std::uint32_t cp = 0;
    if ((b0 & 0xE0) == 0xC0) {
        n = 2;
        cp = b0 & 0x1Fu;
    } else if ((b0 & 0xF0) == 0xE0) {
        n = 3;
        cp = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
        n = 4;
        cp = b0 & 0x07u;
    } else {
        return 0xFFFFFFFFu;
    }
    if (i + n > s.size()) return 0xFFFFFFFFu;
    for (std::size_t k = 1; k < n; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) return 0xFFFFFFFFu;
        cp = (cp << 6) | (b & 0x3Fu);
    }
    len = n;
    return cp;
}

// Same whitespace as Python's str.isspace() (and therefore as Jinja's trim).
bool is_space(std::uint32_t cp) {
    return (cp >= 0x09 && cp <= 0x0D) || (cp >= 0x1C && cp <= 0x20) || cp == 0x85 || cp == 0xA0 ||
           cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 ||
           cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

}  // namespace

std::string_view trim_like_python(std::string_view text) {
    std::size_t begin = text.size(), end = 0;
    for (std::size_t i = 0, len = 0; i < text.size(); i += len) {
        if (!is_space(decode(text, i, len))) {
            if (begin == text.size()) begin = i;
            end = i + len;
        }
    }
    return begin < end ? text.substr(begin, end - begin) : std::string_view{};
}

std::string prompt_prefix() {
    std::string out;
    out += kSystemHeader;
    out += trim_like_python(kSystemPrompt);
    out += kTurnEnd;
    out += kUserHeader;
    return out;
}

std::string prompt_rest(std::string_view segment) {
    std::string out(trim_like_python(segment));
    out += kTurnEnd;
    out += kAssistantHeader;
    return out;
}

std::string render_qwen35(std::string_view segment) { return prompt_prefix() + prompt_rest(segment); }

}  // namespace revisa::infer
