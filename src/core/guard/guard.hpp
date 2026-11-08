#pragma once
// Guards: «ante la duda, no tocar». Evaluated in this order:
//   1 invalid output (empty, thinking marks <think>, the whole sentence wrapped in quotes that
//     the segment did not have)                                        -> segment unchanged
//   2 change ratio > max_change_ratio                                  -> segment unchanged
//   3 edit touching a protected span                                   -> that edit dropped
//   4 edit outside the catalog (or an admitted variant) -> that edit dropped
//   5 more than max_otro OTRO edits among the remaining ones          -> segment unchanged
//   6 type disabled by the user                                        -> that edit dropped
// (A missing <|im_end|> within the cap is the engine's cap_reached; core/check handles it.)

#include <set>
#include <string_view>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/classify/classify.hpp"
#include "core/diff/diff.hpp"
#include "core/guard/config.hpp"
#include "core/protect/protect.hpp"

namespace revisa::guard {

struct Item {
    diff::Edit edit;          // bytes over the (NFC) segment
    classify::Result result;  // type and explanation
};

enum class Rejection { None, InvalidOutput, ChangeRatio, TooManyOtro };

struct Outcome {
    Rejection rejection = Rejection::None;  // != None: the segment stays unchanged, kept is empty
    std::vector<Item> kept;                 // edits that survive, in order
};

// Guard 1 on the raw model output for segment.
bool invalid_output(std::string_view segment, std::string_view output);

// Guard 2: sum over edits of the Levenshtein distance (in code points) between from and to,
// divided by the code points of the segment: the share of characters the correction changes.
double change_ratio(std::string_view segment, const std::vector<diff::Edit>& edits);

// Guard 4: true if the edit is one the catalog never makes: an added comma, an
// added final period, a change of quote/dash style, a change that only touches digits or number
// formatting, lowercasing words written in capitals (emphasis), or an admitted variant.
bool out_of_catalog(const Item& item, std::string_view segment);

// All six guards over the classified edits of one segment.
Outcome apply(std::string_view segment, std::string_view output, std::vector<Item> items,
              const std::vector<protect::Span>& spans, const std::set<catalog::Type>& disabled,
              const Thresholds& thresholds = {});

}  // namespace revisa::guard
