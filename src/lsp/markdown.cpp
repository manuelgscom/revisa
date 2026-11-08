#include "lsp/markdown.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace revisa::lsp {

namespace {

constexpr std::size_t npos = std::string_view::npos;

bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view trim_right(std::string_view s) {
    while (!s.empty() && is_blank(s.back())) s.remove_suffix(1);
    return s;
}

bool only_blanks(std::string_view s) { return trim_right(s).empty(); }

// Up to three spaces of indentation, as CommonMark allows before a fence.
std::string_view skip_indent(std::string_view line) {
    std::size_t i = 0;
    while (i < 3 && i < line.size() && line[i] == ' ') ++i;
    return line.substr(i);
}

struct Fence {
    char c = '`';
    std::size_t n = 0;
};

std::size_t run_of(std::string_view s, std::size_t i, char c) {
    std::size_t n = 0;
    while (i + n < s.size() && s[i + n] == c) ++n;
    return n;
}

std::optional<Fence> opening_fence(std::string_view line) {
    line = skip_indent(line);
    if (line.empty() || (line[0] != '`' && line[0] != '~')) return std::nullopt;
    const Fence f{line[0], run_of(line, 0, line[0])};
    if (f.n < 3) return std::nullopt;
    if (f.c == '`' && line.find('`', f.n) != npos) return std::nullopt;  // a code span, not a fence
    return f;
}

bool closes(std::string_view line, const Fence& f) {
    line = skip_indent(line);
    const std::size_t n = run_of(line, 0, f.c);
    return n >= f.n && only_blanks(line.substr(n));
}

// The start of the first blank line after i (or e): inline constructs never cross it.
std::size_t paragraph_end(std::string_view s, std::size_t i, std::size_t e) {
    for (std::size_t k = s.find('\n', i); k != npos && k < e; k = s.find('\n', k + 1)) {
        std::size_t j = k + 1;
        while (j < e && is_blank(s[j])) ++j;
        if (j >= e || s[j] == '\n') return k + 1;
    }
    return e;
}

class Masker {
public:
    explicit Masker(std::string_view text) : text_(text), out_(text) {}

    void blank(std::size_t b, std::size_t e) {
        for (std::size_t i = b; i < e && i < out_.size(); ++i) {
            if (out_[i] != '\n' && out_[i] != '\r') out_[i] = ' ';
        }
    }

    // Code spans, link destinations, autolinks and HTML over the prose in [b, e).
    void inline_spans(std::size_t b, std::size_t e) {
        std::size_t i = b;
        while (i < e) {
            const char c = text_[i];
            if (c == '\\' && i + 1 < e) {
                i += 2;
            } else if (c == '`') {
                i = code_span(i, e);
            } else if (c == '<') {
                i = tag(i, e);
            } else if (c == ']' && i + 1 < e && text_[i + 1] == '(') {
                i = destination(i + 1, e);
            } else {
                ++i;
            }
        }
    }

    std::string take() { return std::move(out_); }

private:
    // A run of n backticks closed by a run of exactly n; otherwise the run is literal.
    std::size_t code_span(std::size_t i, std::size_t e) {
        const std::size_t n = run_of(text_, i, '`');
        const std::size_t end = paragraph_end(text_, i, e);
        for (std::size_t k = i + n; k < end;) {
            if (text_[k] != '`') {
                ++k;
                continue;
            }
            const std::size_t m = std::min(run_of(text_, k, '`'), end - k);
            if (m == n) {
                blank(i, k + m);
                return k + m;
            }
            k += m;
        }
        return i + n;
    }

    // <tag …>, </tag>, <!-- … -->, <!…>, <?…?> and autolinks <scheme:…>.
    std::size_t tag(std::size_t i, std::size_t e) {
        if (i + 1 >= e) return i + 1;
        const char c = text_[i + 1];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter && c != '/' && c != '!' && c != '?') return i + 1;
        const std::size_t end = paragraph_end(text_, i, e);
        const std::string_view rest = text_.substr(0, end);
        const bool comment = text_.substr(i, 4) == "<!--";
        const std::size_t close = comment ? rest.find("-->", i + 4) : rest.find('>', i + 1);
        if (close == npos) return i + 1;
        const std::size_t after = close + (comment ? 3 : 1);
        blank(i, after);
        return after;
    }

    // The (…) after "](", with nested parentheses; i is at "(".
    std::size_t destination(std::size_t i, std::size_t e) {
        const std::size_t end = paragraph_end(text_, i, e);
        int depth = 0;
        for (std::size_t k = i; k < end; ++k) {
            const char c = text_[k];
            if (c == '\\') {
                ++k;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')' && --depth == 0) {
                blank(i, k + 1);
                return k + 1;
            }
        }
        return i + 1;
    }

    std::string_view text_;
    std::string out_;
};

}  // namespace

std::string mask_markdown(std::string_view text) {
    Masker m(text);

    // Lines: [start, end) without the line break.
    std::vector<std::pair<std::size_t, std::size_t>> lines;
    for (std::size_t start = 0; start <= text.size();) {
        std::size_t nl = text.find('\n', start);
        if (nl == npos) nl = text.size();
        lines.emplace_back(start, nl);
        start = nl + 1;
    }
    const auto line = [&](std::size_t k) {
        return text.substr(lines[k].first, lines[k].second - lines[k].first);
    };

    std::size_t k = 0;
    if (trim_right(line(0)) == "---") {
        for (std::size_t j = 1; j < lines.size(); ++j) {
            const std::string_view l = trim_right(line(j));
            if (l == "---" || l == "...") {
                m.blank(0, lines[j].second);
                k = j + 1;
                break;
            }
        }
    }

    std::size_t prose = k < lines.size() ? lines[k].first : text.size();
    while (k < lines.size()) {
        const std::optional<Fence> fence = opening_fence(line(k));
        if (!fence) {
            ++k;
            continue;
        }
        m.inline_spans(prose, lines[k].first);
        std::size_t j = k + 1;
        while (j < lines.size() && !closes(line(j), *fence)) ++j;
        m.blank(lines[k].first, j < lines.size() ? lines[j].second : text.size());
        k = j + 1;
        prose = k < lines.size() ? lines[k].first : text.size();
    }
    m.inline_spans(prose, text.size());
    return m.take();
}

}  // namespace revisa::lsp
