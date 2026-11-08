#pragma once
// Protected spans: stretches of a segment that are never corrected.
// URLs and bare domains, e-mail addresses, @mentions and #hashtags, `inline code`, file paths,
// numbers with units and times (10 km, 5 %, 18:30, 3,5 GB), and the model's special tokens
// (<|im_start|>, <|im_end|>, <|endoftext|>…). The text is not replaced with placeholders: the
// model sees the whole segment and the guards drop the edits that touch a span.

#include <cstddef>
#include <string_view>
#include <vector>

namespace revisa::protect {

struct Span {
    std::size_t start = 0, end = 0;  // bytes, [start, end)
};

// Sorted, non-overlapping (adjacent or overlapping matches are merged).
std::vector<Span> protect(std::string_view segment);

// true if everything outside the spans is whitespace or punctuation: the segment is skipped.
bool fully_protected(std::string_view segment, const std::vector<Span>& spans);

// true if the segment contains a special token of the model (<|...|>): it is never sent to the
// model, because parse_special would read it as a control token.
bool has_special_token(std::string_view segment);

}  // namespace revisa::protect
