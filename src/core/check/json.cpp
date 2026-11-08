#include "core/check/json.hpp"

#include "core/catalog/catalog.hpp"
#include "core/infer/engine.hpp"

namespace revisa::check {

nlohmann::ordered_json edit_json(const Edit& edit) {
    nlohmann::ordered_json j;
    j["start"] = edit.start;
    j["end"] = edit.end;
    j["from"] = edit.from;
    j["to"] = edit.to;
    j["type"] = std::string(catalog::type_id(edit.type));
    if (edit.explanation) j["explanation"] = *edit.explanation;
    else j["explanation"] = nullptr;
    return j;
}

nlohmann::ordered_json edits_json(const std::vector<Edit>& edits) {
    nlohmann::ordered_json a = nlohmann::ordered_json::array();
    for (const Edit& e : edits) a.push_back(edit_json(e));
    return a;
}

nlohmann::ordered_json response_json(std::string_view model, const Result& result) {
    nlohmann::ordered_json j;
    j["version"] = infer::kContractVersion;
    j["catalog_version"] = catalog::kCatalogVersion;
    j["model"] = std::string(model);
    j["corrected"] = result.corrected;
    j["edits"] = edits_json(result.edits);
    j["segments_skipped"] = result.segments_skipped;
    return j;
}

std::string dump_line(const nlohmann::ordered_json& j) {
    return j.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
}

}  // namespace revisa::check
