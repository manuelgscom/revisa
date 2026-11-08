#pragma once
// Segmenter: splits a text into trimmed sentences, the unit the model sees.
//
// Cuts after a sentence end ('.', '?', '!', '…', possibly followed by closing quotes or
// brackets) that is followed by whitespace and then an uppercase letter, '¿', '¡' or an opening
// quote/bracket before one; and at every line break. Does not cut after the abbreviations of
// src/core/catalog/abreviaturas.txt, inside numbers (3.5, 1.000), URLs, or after single initials
// (J. R. R. Tolkien).
// Each segment is trimmed with the same whitespace as infer::trim_like_python; the whitespace
// around it never reaches the model. A segment longer than infer::kMaxSegmentChars code points is
// cut after ';' or ':' (keeping the sign in the first part); a part still too long is returned
// with too_long = true and is not corrected.

#include <cstddef>
#include <string_view>
#include <vector>

namespace revisa::segment {

struct Segment {
    std::size_t start = 0, end = 0;  // bytes in the text, [start, end), already trimmed, non-empty
    bool too_long = false;
};

// Segments in order; blank stretches produce none. Concatenating the text between and around the
// segments gives back the text (only whitespace lies outside the segments).
std::vector<Segment> segment(std::string_view text);

}  // namespace revisa::segment
