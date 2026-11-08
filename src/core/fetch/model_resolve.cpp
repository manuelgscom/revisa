#include "core/fetch/model_resolve.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "core/fetch/model_info.hpp"

namespace revisa::fetch {

namespace fs = std::filesystem;

namespace {

#if REVISA_TEST_HOOKS
const char* env(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' ? v : nullptr;
}
#endif

}  // namespace

ModelSpec default_model_spec() {
    const ModelInfo& m = kDefaultModel;
    ModelSpec spec{m.name, m.filename, m.url, m.size, m.sha256};
#if REVISA_TEST_HOOKS
    if (const char* url = env("REVISA_TEST_MODEL_URL")) {
        spec.url = url;
        const char* size = env("REVISA_TEST_MODEL_SIZE");
        spec.size = size != nullptr ? std::strtoull(size, nullptr, 10) : 0;
        const char* sha = env("REVISA_TEST_MODEL_SHA256");
        spec.sha256 = sha != nullptr ? sha : "";
    }
#endif
    return spec;
}

ModelResolution resolve_model(const std::string& explicit_path, const std::string& models_dir,
                              const ModelSpec& spec) {
    ModelResolution r;
    std::error_code ec;
    if (!explicit_path.empty()) {
        r.path = explicit_path;
        r.status = fs::exists(fs::path(explicit_path), ec) ? ModelResolution::Status::Ready
                                                           : ModelResolution::Status::Missing;
        return r;
    }
    if (models_dir.empty()) return r;  // NoDir
    const fs::path path = fs::path(models_dir) / spec.filename;
    r.path = path.string();
    const bool ok = fs::is_regular_file(path, ec) && fs::file_size(path, ec) == spec.size && !ec;
    r.status = ok ? ModelResolution::Status::Ready : ModelResolution::Status::NeedsDownload;
    return r;
}

std::string model_name(const std::string& explicit_path, const ModelSpec& spec) {
    if (explicit_path.empty()) return spec.name;
    return fs::path(explicit_path).filename().string();
}

std::uint64_t size_mb(std::uint64_t bytes) {
    constexpr std::uint64_t kMB = 1000 * 1000;
    const std::uint64_t mb = (bytes + kMB / 2) / kMB;
    return mb == 0 && bytes > 0 ? 1 : mb;
}

}  // namespace revisa::fetch
