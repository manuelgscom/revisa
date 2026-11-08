#include "core/classify/classify.hpp"

#include <cstdio>
#include <cstdlib>

#include "core/classify/rules.hpp"
#include "core/infer/prompt.hpp"
#include "core/text/text.hpp"

namespace revisa::classify {

namespace rules {

std::string_view trim(std::string_view s) { return infer::trim_like_python(s); }

bool at_sentence_start(std::string_view left) {
    // Walk back over whitespace and opening signs.
    std::size_t end = left.size();
    while (end > 0) {
        std::size_t start = end - 1;
        while (start > 0 && (static_cast<unsigned char>(left[start]) & 0xC0) == 0x80) --start;
        const char32_t cp = text::decode(left, start).cp;
        if (cp == '\n' || cp == '\r' || cp == 0x2028 || cp == 0x2029) return true;
        if (text::is_space(cp) || cp == U'¿' || cp == U'¡' || cp == U'«' || cp == '"' || cp == '(' ||
            cp == U'“' || cp == '\'' || cp == U'‘') {
            end = start;
            continue;
        }
        return cp == '.' || cp == '?' || cp == '!' || cp == U'…';
    }
    return true;
}

namespace {

bool word_cp(char32_t cp) { return text::is_letter(cp) || text::is_mark(cp); }

}  // namespace

std::string_view last_word(std::string_view left) {
    std::size_t end = left.size();
    // Skip trailing non-letters.
    while (end > 0) {
        std::size_t start = end - 1;
        while (start > 0 && (static_cast<unsigned char>(left[start]) & 0xC0) == 0x80) --start;
        if (word_cp(text::decode(left, start).cp)) break;
        end = start;
    }
    std::size_t begin = end;
    while (begin > 0) {
        std::size_t start = begin - 1;
        while (start > 0 && (static_cast<unsigned char>(left[start]) & 0xC0) == 0x80) --start;
        if (!word_cp(text::decode(left, start).cp)) break;
        begin = start;
    }
    return left.substr(begin, end - begin);
}

std::string_view first_word(std::string_view right) {
    std::size_t i = 0;
    while (i < right.size()) {
        const text::Decoded d = text::decode(right, i);
        if (word_cp(d.cp)) break;
        i += d.len;
    }
    std::size_t j = i;
    while (j < right.size()) {
        const text::Decoded d = text::decode(right, j);
        if (!word_cp(d.cp)) break;
        j += d.len;
    }
    return right.substr(i, j - i);
}

std::string explain(catalog::Type type, std::string_view variant,
                    std::initializer_list<std::pair<std::string_view, std::string_view>> vars) {
    const auto tmpl = catalog::explanation_template(type, variant);
    if (!tmpl) {
        const std::string_view id = catalog::type_id(type);
        std::fprintf(stderr, "revisa: missing explanation %.*s/%.*s\n", static_cast<int>(id.size()), id.data(),
                     static_cast<int>(variant.size()), variant.data());
        std::abort();
    }
    return catalog::fill(*tmpl, vars);
}

Result make(catalog::Type type, std::string_view variant,
            std::initializer_list<std::pair<std::string_view, std::string_view>> vars) {
    return Result{type, explain(type, variant, vars)};
}

}  // namespace rules

namespace {

using Rule = std::optional<Result> (*)(const rules::Input&);

// Catalog order: the table order, with ERRATA moved to the end.
constexpr Rule kRules[] = {
    rules::espaciado,        rules::mayuscula,      rules::tilde_diacritica, rules::tilde_interrogativa,
    rules::por_que,          rules::tilde_falta,    rules::tilde_sobra,      rules::haber_a_ver,
    rules::verbo_haber_h,    rules::echo_hecho,     rules::hay_ahi_ay,       rules::sino_si_no,
    rules::junto_separado,   rules::b_v,            rules::g_j,              rules::h,
    rules::ll_y,             rules::c_z_s_x,        rules::signo_apertura,   rules::dequeismo,
    rules::queismo,          rules::leismo,         rules::laismo,           rules::loismo,
    rules::haber_impersonal, rules::imperativo,     rules::preterito_s,      rules::conjugacion,
    rules::concordancia,     rules::coma_sobra,     rules::errata,
};

}  // namespace

Result classify(std::string_view from, std::string_view to, const Context& ctx) {
    if (from == to) return {};
    rules::Input in;
    in.from = from;
    in.to = to;
    in.from_lower = text::lower(from);
    in.to_lower = text::lower(to);
    in.from_fold = text::fold(from);
    in.to_fold = text::fold(to);
    in.ctx = ctx;
    in.sentence_start = rules::at_sentence_start(ctx.left);
    in.prev_word = rules::last_word(ctx.left);
    in.next_word = rules::first_word(ctx.right);
    for (const Rule rule : kRules)
        if (auto r = rule(in)) return std::move(*r);
    return {};
}

}  // namespace revisa::classify
