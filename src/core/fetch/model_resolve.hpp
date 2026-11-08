#pragma once
// Model file resolution. No network and no prompts: the download dialog lives in the front-ends
// (CLI, tray).
//
//   --model > config `model` > models_dir()/kDefaultModel.filename (with the right size) > download

#include <cstdint>
#include <string>

namespace revisa::fetch {

// The downloadable model: kDefaultModel or, with REVISA_TEST_HOOKS and REVISA_TEST_MODEL_URL set,
// URL, size (REVISA_TEST_MODEL_SIZE) and hash (REVISA_TEST_MODEL_SHA256) from the environment
// (name and file name stay the default ones).
struct ModelSpec {
    std::string name;
    std::string filename;
    std::string url;
    std::uint64_t size = 0;
    std::string sha256;
};

ModelSpec default_model_spec();

struct ModelResolution {
    enum class Status {
        Ready,          // explicit path that exists, or the default file with the right size
        Missing,        // explicit path that does not exist: an error, never downloaded
        NeedsDownload,  // the default file is missing or its size does not match
        NoDir,          // no models directory (see models_dir())
    };
    Status status = Status::NoDir;
    std::string path;
};

// explicit_path: --model or else the config's model (empty = default). models_dir: models_dir().
ModelResolution resolve_model(const std::string& explicit_path, const std::string& models_dir,
                              const ModelSpec& spec);

// The model's display name: the spec's name for the default model, else the file name of the path.
std::string model_name(const std::string& explicit_path, const ModelSpec& spec);

// Decimal megabytes rounded to the nearest (a non-empty file is never 0 MB).
std::uint64_t size_mb(std::uint64_t bytes);

}  // namespace revisa::fetch
