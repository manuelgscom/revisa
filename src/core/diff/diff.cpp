// Token diff (diff.hpp). Decisions the header leaves open, fixed here:
//
// Tokens
//   - An alphanumeric token is a maximal run of letters, combining marks and ASCII digits, so
//     «mp3», «1º», «COVID19» and «3er» are ONE token. Inside the run, an apostrophe (' or ’)
//     joins only between a letter/mark and a letter («O'Donnell»), and '.' or ',' only between
//     two digits («1.000», «3,5»). The token is a Number when it has only digits (and those
//     separators), otherwise a Word. A combining mark with no letter before it starts a Word.
//   - Every other code point is its own Punct token; each invalid byte is its own Punct token.
//
// Alignment
//   - Common prefix and suffix tokens are stripped, then Myers O(ND) runs on the middle,
//     comparing the exact token text (interned to integers). If the edit distance exceeds
//     kMaxD tokens (only on long, mostly different texts) the whole middle becomes ONE edit,
//     without the 1:1 split. Memory stays O(kMaxD²) and time O((N+M)·kMaxD).
//
// Normalising pure insertions/deletions (chunks with one side empty)
//   - The chunk slides over equal tokens to every equivalent position (never merging with a
//     neighbour chunk: at least one equal token stays between them), and the first position, from
//     left to right, of the first class that exists wins:
//       1. starts with a non-space token and ends with a space («de » in «creo de que»);
//       2. starts with a space and ends with a non-space (« Juan» in «Hola Juan.»);
//       3. the leftmost.
//     So the result does not depend on which of the equivalent alignments Myers picked.
//
// Pure deletion of words and commas
//   - One edit per comma plus one per run of the other tokens, so «creo, de que» -> «creo que»
//     gives «,» (COMA_SOBRA) and « de» (DEQUEISMO), not one OTRO. Removing a comma is the only
//     sign change with a type of its own; insertions are never split (they would share a point).
//
// 1:1 split of a chunk with N >= 1 word/number tokens on each side
//   - The words pair in order. The gaps (the punctuation/space tokens before the first word,
//     between two words and after the last) pair in order too: gap k of the original against
//     gap k of the correction. Each gap pair drops its common leading and trailing tokens and
//     what remains, if anything, is one edit («, » -> « » deletes «,»; «» -> «¿» inserts «¿»).
//     Each word pair with different text is one edit. Equal pieces give no edit.
//   - Any edit whose texts are equal is dropped, so from != to always.
#include "core/diff/diff.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include "core/text/text.hpp"

namespace revisa::diff {

namespace {

constexpr std::size_t kMaxD = 1000;

bool is_apostrophe(char32_t cp) { return cp == U'\'' || cp == U'’'; }
bool is_word_cp(char32_t cp) { return text::is_letter(cp) || text::is_mark(cp); }
bool is_wordish(TokenKind k) { return k == TokenKind::Word || k == TokenKind::Number; }

// A run of consecutive changed tokens: original tokens [a0, a1) became corrected tokens [b0, b1).
struct Chunk {
    std::size_t a0, a1, b0, b1;
    bool whole = false;  // fallback chunk of a too distant pair: never split
};

// Myers O(ND) over a and b. Appends the chunks (offset by `off`) or returns false when the
// distance exceeds kMaxD.
bool myers(const std::vector<int>& a, const std::vector<int>& b, std::size_t lo_a, std::size_t hi_a,
           std::size_t lo_b, std::size_t hi_b, std::vector<Chunk>& out) {
    const auto n = static_cast<std::ptrdiff_t>(hi_a - lo_a);
    const auto m = static_cast<std::ptrdiff_t>(hi_b - lo_b);
    auto A = [&](std::ptrdiff_t i) { return a[lo_a + static_cast<std::size_t>(i)]; };
    auto B = [&](std::ptrdiff_t j) { return b[lo_b + static_cast<std::size_t>(j)]; };

    const auto max_d = static_cast<std::ptrdiff_t>(std::min<std::size_t>(kMaxD, static_cast<std::size_t>(n + m)));
    // v[k + off] = furthest x on diagonal k. trace[d] = v over k in [-d, d] after step d.
    const std::ptrdiff_t off = max_d + 1;
    std::vector<std::ptrdiff_t> v(static_cast<std::size_t>(2 * max_d + 3), 0);
    auto V = [&](std::ptrdiff_t k) -> std::ptrdiff_t& { return v[static_cast<std::size_t>(k + off)]; };
    std::vector<std::vector<std::ptrdiff_t>> trace;
    std::ptrdiff_t found = -1;
    for (std::ptrdiff_t d = 0; d <= max_d && found < 0; ++d) {
        for (std::ptrdiff_t k = -d; k <= d; k += 2) {
            std::ptrdiff_t x = (k == -d || (k != d && V(k - 1) < V(k + 1))) ? V(k + 1) : V(k - 1) + 1;
            std::ptrdiff_t y = x - k;
            while (x < n && y < m && A(x) == B(y)) ++x, ++y;
            V(k) = x;
            if (x >= n && y >= m) found = d;
        }
        trace.emplace_back(v.begin() + (off - d), v.begin() + (off + d + 1));
    }
    if (found < 0) return false;

    // Backtrack: ops in reverse ('=' equal, '-' delete a token, '+' insert a token).
    std::vector<char> ops;
    std::ptrdiff_t x = n, y = m;
    for (std::ptrdiff_t d = found; d > 0; --d) {
        const auto& prev = trace[static_cast<std::size_t>(d - 1)];
        auto P = [&](std::ptrdiff_t k) { return prev[static_cast<std::size_t>(k + d - 1)]; };
        const std::ptrdiff_t k = x - y;
        const bool down = k == -d || (k != d && P(k - 1) < P(k + 1));
        const std::ptrdiff_t pk = down ? k + 1 : k - 1;
        const std::ptrdiff_t px = P(pk), py = px - pk;
        const std::ptrdiff_t mx = down ? px : px + 1;  // the snake starts after the move
        for (; x > mx; --x, --y) ops.push_back('=');
        ops.push_back(down ? '+' : '-');
        x = px, y = py;
    }
    for (; x > 0; --x) ops.push_back('=');
    std::reverse(ops.begin(), ops.end());

    std::size_t i = lo_a, j = lo_b;
    for (std::size_t p = 0; p < ops.size();) {
        if (ops[p] == '=') {
            ++i, ++j, ++p;
            continue;
        }
        Chunk c{i, i, j, j};
        for (; p < ops.size() && ops[p] != '='; ++p) (ops[p] == '-' ? c.a1 : c.b1)++;
        i = c.a1, j = c.b1;
        out.push_back(c);
    }
    return true;
}

// Class of a candidate position of a pure chunk (lower is better), see the header comment.
int slide_class(const std::vector<Token>& t, std::size_t s, std::size_t e) {
    const bool first_space = t[s].kind == TokenKind::Space;
    const bool last_space = t[e - 1].kind == TokenKind::Space;
    if (!first_space && last_space) return 0;
    if (first_space && !last_space) return 1;
    return 2;
}

void normalize_pure(std::vector<Chunk>& chunks, const std::vector<Token>& ta, const std::vector<Token>& tb,
                    const std::vector<int>& ia, const std::vector<int>& ib) {
    for (std::size_t k = 0; k < chunks.size(); ++k) {
        Chunk& c = chunks[k];
        const bool del = c.b0 == c.b1;
        const bool ins = c.a0 == c.a1;
        if (del == ins) continue;  // mixed chunk (or empty, which never happens)
        const auto& t = del ? ta : tb;
        const auto& id = del ? ia : ib;
        const std::size_t s = del ? c.a0 : c.b0, e = del ? c.a1 : c.b1;
        const std::size_t lo = k > 0 ? (del ? chunks[k - 1].a1 : chunks[k - 1].b1) + 1 : 0;
        const std::size_t hi = k + 1 < chunks.size() ? (del ? chunks[k + 1].a0 : chunks[k + 1].b0) - 1 : t.size();
        std::size_t left = 0, right = 0;
        while (s - left > lo && id[s - left - 1] == id[e - left - 1]) ++left;
        while (e + right + 1 <= hi && id[s + right] == id[e + right]) ++right;
        std::size_t best = s - left;
        int best_class = slide_class(t, best, best + (e - s));
        for (std::size_t p = s - left + 1; p <= s + right && best_class > 0; ++p) {
            const int cl = slide_class(t, p, p + (e - s));
            if (cl < best_class) best = p, best_class = cl;
        }
        if (best == s) continue;
        const auto delta = static_cast<std::ptrdiff_t>(best) - static_cast<std::ptrdiff_t>(s);
        auto shift = [delta](std::size_t& x) { x = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(x) + delta); };
        shift(c.a0), shift(c.a1), shift(c.b0), shift(c.b1);
    }
}

class Builder {
public:
    Builder(std::string_view o, std::string_view c, const std::vector<Token>& ta, const std::vector<Token>& tb,
            const std::vector<int>& ia, const std::vector<int>& ib)
        : o_(o), c_(c), ta_(ta), tb_(tb), ia_(ia), ib_(ib) {}

    void chunk(const Chunk& ch) {
        std::vector<std::size_t> wa, wb;
        for (std::size_t i = ch.a0; i < ch.a1; ++i)
            if (is_wordish(ta_[i].kind)) wa.push_back(i);
        for (std::size_t j = ch.b0; j < ch.b1; ++j)
            if (is_wordish(tb_[j].kind)) wb.push_back(j);
        // A pure deletion of words and commas («creo, de que» -> «creo que») splits into one edit
        // per comma plus one per run of the other tokens: «,» (COMA_SOBRA) and « de».
        if (!ch.whole && ch.b0 == ch.b1 && !wa.empty()) {
            auto is_comma = [&](std::size_t i) { return o_.substr(ta_[i].start, ta_[i].end - ta_[i].start) == ","; };
            bool has_comma = false;
            for (std::size_t i = ch.a0; i < ch.a1; ++i) has_comma = has_comma || is_comma(i);
            if (has_comma) {
                std::size_t run = ch.a0;
                for (std::size_t i = ch.a0; i < ch.a1; ++i) {
                    if (!is_comma(i)) continue;
                    if (run < i) emit(run, i, ch.b0, ch.b0);
                    emit(i, i + 1, ch.b0, ch.b0);
                    run = i + 1;
                }
                if (run < ch.a1) emit(run, ch.a1, ch.b0, ch.b0);
                return;
            }
        }
        if (ch.whole || wa.empty() || wa.size() != wb.size()) {
            emit(ch.a0, ch.a1, ch.b0, ch.b1);
            return;
        }
        std::size_t pa = ch.a0, pb = ch.b0;
        for (std::size_t k = 0; k <= wa.size(); ++k) {
            const bool last = k == wa.size();
            gap(pa, last ? ch.a1 : wa[k], pb, last ? ch.b1 : wb[k]);
            if (last) break;
            emit(wa[k], wa[k] + 1, wb[k], wb[k] + 1);
            pa = wa[k] + 1, pb = wb[k] + 1;
        }
    }

    std::vector<Edit> take() { return std::move(edits_); }

private:
    std::size_t pos_a(std::size_t i) const { return i < ta_.size() ? ta_[i].start : o_.size(); }
    std::size_t pos_b(std::size_t j) const { return j < tb_.size() ? tb_[j].start : c_.size(); }

    // Gap of punctuation/spaces: strip the common leading and trailing tokens.
    void gap(std::size_t a0, std::size_t a1, std::size_t b0, std::size_t b1) {
        while (a0 < a1 && b0 < b1 && ia_[a0] == ib_[b0]) ++a0, ++b0;
        while (a0 < a1 && b0 < b1 && ia_[a1 - 1] == ib_[b1 - 1]) --a1, --b1;
        emit(a0, a1, b0, b1);
    }

    void emit(std::size_t a0, std::size_t a1, std::size_t b0, std::size_t b1) {
        Edit e;
        e.start = pos_a(a0), e.end = pos_a(a1);
        e.to_start = pos_b(b0), e.to_end = pos_b(b1);
        e.from = std::string(o_.substr(e.start, e.end - e.start));
        e.to = std::string(c_.substr(e.to_start, e.to_end - e.to_start));
        if (e.from != e.to) edits_.push_back(std::move(e));
    }

    std::string_view o_, c_;
    const std::vector<Token>&ta_, &tb_;
    const std::vector<int>&ia_, &ib_;
    std::vector<Edit> edits_;
};

}  // namespace

std::vector<Token> tokenize(std::string_view s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const std::size_t start = i;
        const text::Decoded d = text::decode(s, i);
        if (text::is_space(d.cp)) {
            i += d.len;
            while (i < s.size()) {
                const text::Decoded n = text::decode(s, i);
                if (!text::is_space(n.cp)) break;
                i += n.len;
            }
            out.push_back({TokenKind::Space, start, i});
        } else if (is_word_cp(d.cp) || text::is_digit(d.cp)) {
            bool word = false;
            char32_t prev = 0;
            while (i < s.size()) {
                const text::Decoded c = text::decode(s, i);
                if (is_word_cp(c.cp)) {
                    word = true;
                } else if (!text::is_digit(c.cp)) {
                    if (i + c.len >= s.size()) break;
                    const char32_t next = text::decode(s, i + c.len).cp;
                    const bool joins = (is_apostrophe(c.cp) && is_word_cp(prev) && text::is_letter(next)) ||
                                       ((c.cp == U'.' || c.cp == U',') && text::is_digit(prev) && text::is_digit(next));
                    if (!joins) break;
                }
                prev = c.cp;
                i += c.len;
            }
            out.push_back({word ? TokenKind::Word : TokenKind::Number, start, i});
        } else {
            i += d.len;
            out.push_back({TokenKind::Punct, start, i});
        }
    }
    return out;
}

std::vector<Edit> diff(std::string_view original, std::string_view corrected) {
    if (original == corrected) return {};
    const std::vector<Token> ta = tokenize(original), tb = tokenize(corrected);

    std::unordered_map<std::string_view, int> intern;
    auto ids = [&intern](std::string_view s, const std::vector<Token>& t) {
        std::vector<int> out;
        out.reserve(t.size());
        for (const Token& tok : t)
            out.push_back(intern.emplace(s.substr(tok.start, tok.end - tok.start), static_cast<int>(intern.size()))
                              .first->second);
        return out;
    };
    const std::vector<int> ia = ids(original, ta), ib = ids(corrected, tb);

    std::size_t pre = 0;
    while (pre < ia.size() && pre < ib.size() && ia[pre] == ib[pre]) ++pre;
    std::size_t suf = 0;
    while (suf < ia.size() - pre && suf < ib.size() - pre && ia[ia.size() - 1 - suf] == ib[ib.size() - 1 - suf]) ++suf;
    const std::size_t hi_a = ia.size() - suf, hi_b = ib.size() - suf;

    std::vector<Chunk> chunks;
    if (!myers(ia, ib, pre, hi_a, pre, hi_b, chunks)) {
        chunks.assign(1, Chunk{pre, hi_a, pre, hi_b, true});
    } else {
        normalize_pure(chunks, ta, tb, ia, ib);
    }

    Builder builder(original, corrected, ta, tb, ia, ib);
    for (const Chunk& c : chunks) builder.chunk(c);
    return builder.take();
}

std::string apply(std::string_view original, const std::vector<Edit>& edits) {
    std::string out;
    std::size_t pos = 0;
    for (const Edit& e : edits) {
        out.append(original.substr(pos, e.start - pos));
        out += e.to;
        pos = e.end;
    }
    out.append(original.substr(pos));
    return out;
}

}  // namespace revisa::diff
