#pragma once
// JSON of a check result, shared by the CLI, the server and the LSP.
//   response: {"version", "catalog_version", "model", "corrected", "edits": [EDIT…],
//              "segments_skipped"}
//   EDIT:     {"start", "end", "from", "to", "type", "explanation"} (UTF-16 offsets over the
//             original text; explanation null for OTRO)

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/check/check.hpp"

namespace revisa::check {

nlohmann::ordered_json edit_json(const Edit& edit);
nlohmann::ordered_json edits_json(const std::vector<Edit>& edits);

// The API response of one text.
nlohmann::ordered_json response_json(std::string_view model, const Result& result);

// j compact, followed by "\n". Invalid UTF-8 is replaced with U+FFFD.
std::string dump_line(const nlohmann::ordered_json& j);

}  // namespace revisa::check
