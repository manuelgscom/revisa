#include "cli/report.hpp"

#include "core/catalog/catalog.hpp"
#include "core/infer/engine.hpp"

namespace revisa::cli {

// check::response_json with "file" first (only for files).
std::string json_line(const std::optional<std::string>& file, std::string_view model, const check::Result& result) {
    nlohmann::ordered_json j;
    if (file) j["file"] = *file;
    j["version"] = infer::kContractVersion;
    j["catalog_version"] = catalog::kCatalogVersion;
    j["model"] = std::string(model);
    j["corrected"] = result.corrected;
    j["edits"] = edits_json(result.edits);
    j["segments_skipped"] = result.segments_skipped;
    return dump_line(j);
}

}  // namespace revisa::cli
