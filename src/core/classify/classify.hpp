#pragma once
// Classifier: the type and the fixed explanation of one edit. A pure function of
// the edit and its context; the rules run in catalog order and the first that matches wins,
// except ERRATA, which runs last (just before OTRO).

#include <optional>
#include <string>
#include <string_view>

#include "core/catalog/catalog.hpp"

namespace revisa::classify {

struct Context {
    std::string_view left;   // original text before the edit (within the segment or the pair)
    std::string_view right;  // original text after the edit
};

struct Result {
    catalog::Type type = catalog::Type::Otro;
    std::optional<std::string> explanation;  // nullopt only for OTRO
};

// from/to: the exact texts of the edit (diff::Edit::from / to), spaces included.
Result classify(std::string_view from, std::string_view to, const Context& ctx = {});

}  // namespace revisa::classify
