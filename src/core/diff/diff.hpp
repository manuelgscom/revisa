#pragma once
// Token diff between a segment and its correction.
//
// Tokens: word (letters, combining marks, internal apostrophes ' and ’ between letters), number
// (ASCII digits with internal '.' or ',' between digits), space (a run of text::is_space code
// points is ONE token), and every other code point is its own token (punctuation, symbols, emoji).
//
// diff(): Myers over the tokens, grouped into chunks of consecutive changes. Then:
//   1. A chunk with the same number N of word/number tokens on both sides is split into edits
//      that pair them in order (e.g. «e echo» -> «he hecho» gives two edits).
//      Inserted/deleted punctuation inside the chunk becomes its own edit
//      («que» -> «¿Qué» gives an insertion of «¿» and «que» -> «Qué»).
//   2. Otherwise the chunk is one edit («haber» -> «a ver»).
//   3. A pure insertion or deletion of a word carries one adjacent space token, on the right
//      when there is one («creo de que» -> «creo que» deletes «de »), else on the left.
// Edits come sorted by start and never overlap (an insertion at p may precede an edit [p, e)).
// The result is deterministic for a given pair: the benchmark scorer relies on it.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace revisa::diff {

enum class TokenKind { Word, Number, Space, Punct };

struct Token {
    TokenKind kind;
    std::size_t start = 0, end = 0;  // bytes, [start, end)
};

std::vector<Token> tokenize(std::string_view text);

struct Edit {
    std::size_t start = 0, end = 0;        // bytes in the original, [start, end); start == end: insertion
    std::size_t to_start = 0, to_end = 0;  // bytes in the corrected text
    std::string from;                      // original[start, end)
    std::string to;                        // corrected[to_start, to_end)
};

std::vector<Edit> diff(std::string_view original, std::string_view corrected);

// original with the edits applied (edits sorted and not overlapping, as diff() returns them).
// apply(o, diff(o, c)) == c for every o, c.
std::string apply(std::string_view original, const std::vector<Edit>& edits);

}  // namespace revisa::diff
