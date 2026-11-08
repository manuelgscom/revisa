#include "lsp/position.hpp"

#include <algorithm>

#include "core/text/text.hpp"

namespace revisa::lsp {

std::string_view encoding_name(Encoding e) {
    switch (e) {
        case Encoding::Utf8: return "utf-8";
        case Encoding::Utf16: return "utf-16";
        case Encoding::Utf32: return "utf-32";
    }
    return "utf-16";
}

std::optional<Encoding> parse_encoding(std::string_view name) {
    if (name == "utf-8") return Encoding::Utf8;
    if (name == "utf-16") return Encoding::Utf16;
    if (name == "utf-32") return Encoding::Utf32;
    return std::nullopt;
}

LineMap::LineMap(std::string_view text) : text_(text) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n' && text[i] != '\r') continue;
        starts_.push_back(start);
        ends_.push_back(i);
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        start = i + 1;
    }
    starts_.push_back(start);
    ends_.push_back(text.size());
}

std::size_t LineMap::line_of(std::size_t byte) const {
    const auto it = std::upper_bound(starts_.begin(), starts_.end(), byte);
    return static_cast<std::size_t>(it - starts_.begin()) - 1;
}

namespace {

// Units of one code point of length len in encoding e.
std::size_t units(char32_t cp, std::size_t len, Encoding e) {
    switch (e) {
        case Encoding::Utf8: return len;
        case Encoding::Utf16: return cp >= 0x10000 ? 2 : 1;
        case Encoding::Utf32: return 1;
    }
    return 1;
}

}  // namespace

Position LineMap::position(std::size_t byte, Encoding e) const {
    byte = std::min(byte, text_.size());
    const std::size_t line = line_of(byte);
    const std::size_t end = std::min(byte, ends_[line]);
    std::size_t character = 0;
    for (std::size_t i = starts_[line]; i < end;) {
        const text::Decoded d = text::decode(text_, i);
        character += units(d.cp, d.len, e);
        i += d.len;
    }
    return {line, character};
}

std::size_t LineMap::byte(Position p, Encoding e) const {
    if (p.line >= starts_.size()) return text_.size();
    std::size_t i = starts_[p.line];
    std::size_t character = 0;
    while (i < ends_[p.line]) {
        const text::Decoded d = text::decode(text_, i);
        const std::size_t u = units(d.cp, d.len, e);
        if (character + u > p.character) break;
        character += u;
        i += d.len;
    }
    return i;
}

}  // namespace revisa::lsp
