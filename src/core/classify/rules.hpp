#pragma once
// Internal to the classifier: one function per catalog type. classify.cpp builds the Input and
// calls them in catalog order (ERRATA last). Each returns the result if its rule matches the edit,
// nullopt otherwise. A rule never needs to know about the others: the order resolves overlaps.
//   ortografia.cpp: types 1-20 (ESPACIADO … ERRATA)
//   gramatica.cpp:  types 21-31 (DEQUEISMO … COMA_SOBRA)

#include <optional>
#include <string>
#include <string_view>

#include "core/classify/classify.hpp"

namespace revisa::classify::rules {

struct Input {
    std::string_view from, to;        // exact edit texts (may include spaces)
    std::string from_lower, to_lower; // text::lower
    std::string from_fold, to_fold;   // text::fold: lowercase, no accents (ñ kept)
    Context ctx;
    bool sentence_start = false;      // the edit starts a sentence (see at_sentence_start)
    std::string_view prev_word;       // last word token of ctx.left ("" if none)
    std::string_view next_word;       // first word token of ctx.right ("" if none)
};

// --- Helpers (classify.cpp) -----------------------------------------------------------------

// true if left is empty or only whitespace, or ends (ignoring whitespace and opening ¿ ¡ « " ( )
// with '.', '?', '!', '…' or a line break.
bool at_sentence_start(std::string_view left);
std::string_view last_word(std::string_view left);
std::string_view first_word(std::string_view right);
std::string_view trim(std::string_view s);  // infer::trim_like_python
// The explanation of (type, variant) filled with vars; aborts if the template is missing.
std::string explain(catalog::Type type, std::string_view variant,
                    std::initializer_list<std::pair<std::string_view, std::string_view>> vars = {});
// Result{type, explain(type, variant, vars)}.
Result make(catalog::Type type, std::string_view variant = {},
            std::initializer_list<std::pair<std::string_view, std::string_view>> vars = {});

// --- Rules -----------------------------------------------------------------------------------

std::optional<Result> espaciado(const Input&);
std::optional<Result> mayuscula(const Input&);
std::optional<Result> tilde_diacritica(const Input&);
std::optional<Result> tilde_interrogativa(const Input&);
std::optional<Result> por_que(const Input&);
std::optional<Result> tilde_falta(const Input&);
std::optional<Result> tilde_sobra(const Input&);
std::optional<Result> haber_a_ver(const Input&);
std::optional<Result> verbo_haber_h(const Input&);
std::optional<Result> echo_hecho(const Input&);
std::optional<Result> hay_ahi_ay(const Input&);
std::optional<Result> sino_si_no(const Input&);
std::optional<Result> junto_separado(const Input&);
std::optional<Result> b_v(const Input&);
std::optional<Result> g_j(const Input&);
std::optional<Result> h(const Input&);
std::optional<Result> ll_y(const Input&);
std::optional<Result> c_z_s_x(const Input&);
std::optional<Result> signo_apertura(const Input&);
std::optional<Result> errata(const Input&);

std::optional<Result> dequeismo(const Input&);
std::optional<Result> queismo(const Input&);
std::optional<Result> leismo(const Input&);
std::optional<Result> laismo(const Input&);
std::optional<Result> loismo(const Input&);
std::optional<Result> haber_impersonal(const Input&);
std::optional<Result> imperativo(const Input&);
std::optional<Result> preterito_s(const Input&);
std::optional<Result> conjugacion(const Input&);
std::optional<Result> concordancia(const Input&);
std::optional<Result> coma_sobra(const Input&);

}  // namespace revisa::classify::rules
