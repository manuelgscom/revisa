#pragma once
// Markdown exclusions of the LSP: what is not prose is masked with spaces before the
// check, so the model never sees it and no edit falls inside it.

#include <string>
#include <string_view>

namespace revisa::lsp {

// text with these parts replaced by spaces, byte for byte (line breaks kept, same length, so every
// byte offset is the same in both): the front matter (a leading `---` … `---` or `...`), fenced
// code blocks (``` and ~~~, unclosed ones to the end), code spans, the `(…)` of links and images,
// autolinks and HTML tags and comments. A construct never crosses a blank line, except fences.
std::string mask_markdown(std::string_view text);

}  // namespace revisa::lsp
