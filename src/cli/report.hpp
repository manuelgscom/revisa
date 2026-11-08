#pragma once
// JSON output of the CLI, compact, one object per line.
//   check --json: {"file": PATH (only for files), "version", "catalog_version", "model",
//                  "corrected", "edits": [EDIT…], "segments_skipped"}
//   check --jsonl: {"id" (if given), "corrected", "edits": [EDIT…], "segments_skipped", "changed",
//                  "replay_dropped" (only with --replay)}
//   diff --json:  {"edits": [EDIT…]}
//   diff --jsonl: {"id" (if given), "edits": [EDIT + "out_of_catalog"…]}
//   EDIT:         {"start", "end", "from", "to", "type", "explanation"} (UTF-16 offsets;
//                 explanation null for OTRO)

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/check/check.hpp"
#include "core/check/json.hpp"

namespace revisa::cli {

// The serializers live in core/check/json.hpp (shared with the server and the LSP).
using check::dump_line;
using check::edit_json;
using check::edits_json;

// The check --json line.
std::string json_line(const std::optional<std::string>& file, std::string_view model, const check::Result& result);

}  // namespace revisa::cli
