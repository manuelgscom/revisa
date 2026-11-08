#pragma once
// Text utilities shared by the deterministic modules (segment, protect, diff, classify, guard).
//
// Everything inside the engine is UTF-8. Invalid bytes never abort: each invalid byte decodes as
// U+FFFD with length 1 (and counts as one UTF-16 unit, as the JSON output replaces it with one
// U+FFFD). Case and accent handling covers Latin (ASCII, Latin-1, Latin Extended-A/B and
// Latin Extended Additional), plus basic Greek and Cyrillic case; other scripts pass unchanged.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace revisa::text {

inline constexpr char32_t kReplacement = 0xFFFD;

struct Decoded {
    char32_t cp;      // kReplacement for an invalid byte
    std::size_t len;  // bytes consumed, >= 1
};

// The code point starting at byte i (i < s.size()).
Decoded decode(std::string_view s, std::size_t i);

// Appends cp as UTF-8.
void append_utf8(std::string& out, char32_t cp);

// Code points of s (an invalid byte counts as one).
std::size_t code_points(std::string_view s);

// UTF-16 code units of s (an invalid byte counts as one; U+10000 and above count as two).
std::size_t utf16_length(std::string_view s);

// Byte offset -> UTF-16 offset over one text, in O(1) per query after an O(n) build.
// at(b) == utf16_length(text.substr(0, b)) for every b at a code point boundary (0..size()).
class Utf16Index {
public:
    explicit Utf16Index(std::string_view text);
    std::size_t at(std::size_t byte) const;

private:
    std::vector<std::size_t> units_;  // size() + 1
};

// --- Character classes ------------------------------------------------------------------------

// Python's str.isspace() (the same set as infer::trim_like_python).
bool is_space(char32_t cp);
// Combining marks: U+0300-036F, U+1AB0-1AFF, U+1DC0-1DFF, U+20D0-20FF, U+FE20-FE2F.
bool is_mark(char32_t cp);
// Letters: ASCII, ª µ º, Latin-1 and Latin Extended (U+00C0-024F minus × ÷), Latin Extended
// Additional, Greek and Cyrillic. Combining marks are not letters (see is_mark).
bool is_letter(char32_t cp);
bool is_digit(char32_t cp);  // ASCII 0-9 only

// --- Case and accents (Latin, Greek, Cyrillic) -------------------------------------------------

char32_t to_lower(char32_t cp);
char32_t to_upper(char32_t cp);
bool is_upper(char32_t cp);  // to_lower(cp) != cp
bool is_lower(char32_t cp);  // to_upper(cp) != cp

std::string lower(std::string_view s);
std::string upper(std::string_view s);

// Removes accents and diaeresis from Latin letters (á→a, Ü→U, ç→c) and drops combining marks,
// but keeps ñ and Ñ: in Spanish «año» and «ano» are different words.
std::string strip_accents(std::string_view s);

// lower(strip_accents(s)): the comparison key of the classifier ("≈ sin tildes", ignoring case).
std::string fold(std::string_view s);

// --- NFC (Latin only) -------------------------------------------------------------------------

// NFC restricted to the table of src/core/text/nfc_table.inc: canonical decomposition,
// canonical ordering of the combining marks and composition, for composed characters in
// U+00C0-024F and U+1E00-1EFF. Anything else passes unchanged. On those characters it equals
// Python's unicodedata.normalize("NFC", s) (tests/text/nfc_random.jsonl).
//
// map(start, end) takes a byte range of text (at code point boundaries) back to the original
// string. A character that NFC changed maps to its whole original cluster (base + marks); an
// unchanged one maps exactly. map(i, i) is an insertion point and returns (j, j).
struct Normalized {
    std::string text;
    std::pair<std::size_t, std::size_t> map(std::size_t start, std::size_t end) const;

    // Per byte of text: the original range of the code point (or cluster) that contains it.
    std::vector<std::size_t> orig_start, orig_end;
    std::size_t orig_size = 0;
};

Normalized nfc(std::string_view s);

// Shortcut: nfc(s).text.
std::string to_nfc(std::string_view s);

}  // namespace revisa::text
