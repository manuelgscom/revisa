// core/text: UTF-8, UTF-16 offsets, case, accents and the Latin NFC (against Python's NFC).
#include "doctest.h"

#include <string>

#include "core/infer/prompt.hpp"
#include "core/text/text.hpp"
#include "jsonl.hpp"

using namespace revisa;

TEST_CASE("decode: valid, invalid and overlong sequences") {
    CHECK(text::decode("a", 0).cp == U'a');
    CHECK(text::decode("ñ", 0).cp == U'ñ');
    CHECK(text::decode("ñ", 0).len == 2);
    CHECK(text::decode("😀", 0).cp == U'😀');
    CHECK(text::decode("😀", 0).len == 4);
    CHECK(text::decode("\xC3", 0).cp == text::kReplacement);
    CHECK(text::decode("\xC3", 0).len == 1);
    CHECK(text::decode("\xC0\xAF", 0).cp == text::kReplacement);    // overlong
    CHECK(text::decode("\xED\xA0\x80", 0).cp == text::kReplacement);  // surrogate
    std::string s;
    text::append_utf8(s, U'€');
    text::append_utf8(s, U'😀');
    CHECK(s == "€😀");
}

TEST_CASE("code points and UTF-16 units") {
    CHECK(text::code_points("año") == 3);
    CHECK(text::utf16_length("año") == 3);
    CHECK(text::utf16_length("a😀b") == 4);
    CHECK(text::utf16_length("e\xCC\x81") == 2);  // e + combining acute
    CHECK(text::utf16_length("\xFF") == 1);       // invalid byte = one U+FFFD
    const std::string t = "Ñu 😀 é";
    const text::Utf16Index idx(t);
    CHECK(idx.at(0) == 0);
    CHECK(idx.at(2) == 1);   // after Ñ
    CHECK(idx.at(4) == 3);   // before 😀
    CHECK(idx.at(8) == 5);   // after 😀
    CHECK(idx.at(t.size()) == text::utf16_length(t));
}

TEST_CASE("is_space matches trim_like_python") {
    for (const char32_t cp : {U' ', U'\t', U'\n', U' ', U' ', U'　', U'\u0085'}) {
        CHECK(text::is_space(cp));
        std::string s;
        text::append_utf8(s, cp);
        CHECK(infer::trim_like_python(s + "x" + s) == "x");
    }
    CHECK_FALSE(text::is_space(U'a'));
    CHECK_FALSE(text::is_space(U'​'));  // zero width space is not whitespace for Python
}

TEST_CASE("classes") {
    CHECK(text::is_letter(U'ñ'));
    CHECK(text::is_letter(U'Ü'));
    CHECK(text::is_letter(U'ª'));
    CHECK_FALSE(text::is_letter(U'×'));
    CHECK_FALSE(text::is_letter(U'1'));
    CHECK_FALSE(text::is_letter(U'¿'));
    CHECK_FALSE(text::is_letter(U'😀'));
    CHECK(text::is_mark(0x0301));
    CHECK(text::is_digit(U'7'));
}

TEST_CASE("case and accents") {
    CHECK(text::lower("ÁRBOL Ñandú ÜBER ŸŒ") == "árbol ñandú über ÿœ");
    CHECK(text::upper("árbol ñandú über") == "ÁRBOL ÑANDÚ ÜBER");
    CHECK(text::strip_accents("Árbol, pingüino, año, Ñandú") == "Arbol, pinguino, año, Ñandu");
    CHECK(text::fold("QUÉ Él MAÑANA") == "que el mañana");
    CHECK(text::strip_accents("e\xCC\x81") == "e");  // decomposed accent dropped
    CHECK(text::lower("\xFF" "A") == "\xFF" "a");     // invalid bytes kept
    CHECK(text::is_upper(U'Ñ'));
    CHECK(text::is_lower(U'ñ'));
    CHECK_FALSE(text::is_upper(U'1'));
}

TEST_CASE("nfc: composition and offset map") {
    const std::string decomposed = "cafe\xCC\x81 y an\xCC\x83o";  // café y año, decomposed
    const text::Normalized n = text::nfc(decomposed);
    CHECK(n.text == "café y año");
    // «café» in the normalized text is bytes [0, 5); in the original, [0, 6).
    CHECK(n.map(0, 5) == std::pair<std::size_t, std::size_t>{0, 6});
    // «é» alone maps to its whole cluster e + U+0301.
    CHECK(n.map(3, 5) == std::pair<std::size_t, std::size_t>{3, 6});
    // «y» is unchanged and maps exactly.
    CHECK(n.map(6, 7) == std::pair<std::size_t, std::size_t>{7, 8});
    // An insertion point maps to an insertion point; the end maps to the end.
    CHECK(n.map(6, 6) == std::pair<std::size_t, std::size_t>{7, 7});
    CHECK(n.map(n.text.size(), n.text.size()) ==
          std::pair<std::size_t, std::size_t>{decomposed.size(), decomposed.size()});
    // Already NFC text is unchanged, byte for byte, with an exact map.
    const std::string ok = "Ya he hecho el informe 😀 ñ";
    const text::Normalized m = text::nfc(ok);
    CHECK(m.text == ok);
    CHECK(m.map(3, 5) == std::pair<std::size_t, std::size_t>{3, 5});
    // Invalid bytes pass through.
    CHECK(text::to_nfc("a\xFF" "b") == "a\xFF" "b");
}

TEST_CASE("nfc equals Python's unicodedata NFC on random Latin strings") {
    int n = 0;
    for (const auto& c : test::load_jsonl(std::string(REVISA_TESTS_DIR) + "/text/nfc_random.jsonl")) {
        const std::string in = c.j.at("text"), want = c.j.at("nfc");
        CHECK_MESSAGE(text::to_nfc(in) == want, c.where);
        ++n;
    }
    CHECK(n == 2000);
}
