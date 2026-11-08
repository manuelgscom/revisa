#pragma once
// LSP positions: (line, character) over a text, with character counted in the negotiated
// encoding (UTF-8 bytes, UTF-16 code units or code points). Line breaks: \n, \r\n and \r.

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace revisa::lsp {

enum class Encoding { Utf8, Utf16, Utf32 };

// "utf-8", "utf-16", "utf-32".
std::string_view encoding_name(Encoding e);
std::optional<Encoding> parse_encoding(std::string_view name);

struct Position {
    std::size_t line = 0;
    std::size_t character = 0;
    bool operator==(const Position&) const = default;
};

// The lines of a text. Keeps a view: the text must outlive the map.
class LineMap {
public:
    explicit LineMap(std::string_view text);

    std::size_t line_count() const { return starts_.size(); }
    std::size_t line_start(std::size_t line) const { return starts_[line]; }
    std::size_t line_end(std::size_t line) const { return ends_[line]; }  // before the line break
    std::size_t line_of(std::size_t byte) const;

    // byte must be at a code point boundary; inside a line break it counts as the line's end.
    Position position(std::size_t byte, Encoding e) const;

    // The byte of a position. A line past the end gives the text's end; a character past the end
    // of its line gives the line's end; a character inside a code point gives its start.
    std::size_t byte(Position p, Encoding e) const;

private:
    std::string_view text_;
    std::vector<std::size_t> starts_, ends_;
};

}  // namespace revisa::lsp
