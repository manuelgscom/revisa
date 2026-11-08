#include <string>
#include <string_view>

#include "core/infer/prompt.hpp"
#include "doctest.h"

using namespace revisa::infer;

TEST_CASE("trim_like_python: ASCII whitespace") {
    CHECK(trim_like_python("  hola  ") == "hola");
    CHECK(trim_like_python("\t\n\r\v\fhola mundo\n") == "hola mundo");
    CHECK(trim_like_python("\x1c\x1fhola\x1d") == "hola");
    CHECK(trim_like_python("hola") == "hola");
    CHECK(trim_like_python("a  b") == "a  b");
}

TEST_CASE("trim_like_python: Unicode whitespace") {
    CHECK(trim_like_python(" hola ") == "hola");                    // NBSP
    CHECK(trim_like_python("　hola　") == "hola");                    // ideographic space
    CHECK(trim_like_python(" hola ") == "hola");                    // line/paragraph separator
    CHECK(trim_like_python("\u0085     hola") == "hola");
    CHECK(trim_like_python("mañana ") == "mañana");            // ñ is kept
}

TEST_CASE("trim_like_python: U+200B is not whitespace") {
    CHECK(trim_like_python("​hola​") == "​hola​");
    CHECK(trim_like_python(" ​ ") == "​");
}

TEST_CASE("trim_like_python: invalid UTF-8 is kept") {
    CHECK(trim_like_python(" \xff ") == "\xff");
    CHECK(trim_like_python("\xc3") == "\xc3");             // truncated sequence
    CHECK(trim_like_python(" hola\xe2\x80 ") == "hola\xe2\x80");
    CHECK(trim_like_python("\xc2 ") == "\xc2");            // lead byte + ASCII space
}

TEST_CASE("trim_like_python: all whitespace or empty gives empty") {
    CHECK(trim_like_python("").empty());
    CHECK(trim_like_python(" \t\n").empty());
    CHECK(trim_like_python(" 　  ").empty());
}

TEST_CASE("system prompt of contract v1") {
    CHECK(kSystemPrompt ==
          "Corrige los errores de ortografía y gramática del texto según la norma del español. "
          "No cambies el estilo, el vocabulario ni el registro. Si el texto es correcto, devuélvelo igual. "
          "Responde solo con el texto.");
    CHECK(trim_like_python(kSystemPrompt) == kSystemPrompt);
}

TEST_CASE("prompt_prefix ends with the user header") {
    const std::string p = prompt_prefix();
    CHECK(p.starts_with("<|im_start|>system\n"));
    CHECK(p.ends_with("<|im_end|>\n<|im_start|>user\n"));
}

TEST_CASE("prefix + rest == render") {
    for (const std::string_view s :
         {std::string_view("haber si mañana podemos vernos"), std::string_view("  hola  "),
          std::string_view(" Primera línea.\nSegunda línea.　"), std::string_view("a\n\nb"),
          std::string_view("«comillas» y \"dobles\" 😀"), std::string_view("")}) {
        CAPTURE(s);
        CHECK(prompt_prefix() + prompt_rest(s) == render_qwen35(s));
    }
}

TEST_CASE("render of a known segment is the exact contract string") {
    const std::string expected =
        "<|im_start|>system\n"
        "Corrige los errores de ortografía y gramática del texto según la norma del español. "
        "No cambies el estilo, el vocabulario ni el registro. Si el texto es correcto, devuélvelo igual. "
        "Responde solo con el texto.<|im_end|>\n"
        "<|im_start|>user\nhaber si mañana podemos vernos<|im_end|>\n"
        "<|im_start|>assistant\n<think>\n\n</think>\n\n";
    CHECK(render_qwen35("haber si mañana podemos vernos") == expected);
    CHECK(render_qwen35("  haber si mañana podemos vernos\n") == expected);
    CHECK(render_qwen35("a\nb").find("user\na\nb<|im_end|>") != std::string::npos);
}

TEST_CASE("max_new_tokens is ceil(1.5 n) + 8") {
    CHECK(max_new_tokens(0) == 8);
    CHECK(max_new_tokens(1) == 10);
    CHECK(max_new_tokens(2) == 11);
    CHECK(max_new_tokens(3) == 13);
    CHECK(max_new_tokens(10) == 23);
}
