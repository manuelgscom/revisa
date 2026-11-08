#include "core/infer/fake_engine.hpp"

#if REVISA_TEST_HOOKS

#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/infer/prompt.hpp"

namespace revisa::infer {

namespace {

std::string env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return v != nullptr ? v : fallback;
}

// Answers fixed by the REVISA_FAKE_* variables, read once when the engine is created.
class FakeEngine : public Engine {
public:
    FakeEngine() {
        if (std::getenv("REVISA_FAKE_ERROR") != nullptr) {
            mode_ = Mode::Error;
            error_ = env_or("REVISA_FAKE_ERROR", "");
            error_detail_ = env_or("REVISA_FAKE_ERROR_DETAIL", "");
        } else if (const char* path = std::getenv("REVISA_FAKE_MAP")) {
            mode_ = Mode::Map;
            load_map(path);
        } else {
            mode_ = Mode::Output;
            output_ = env_or("REVISA_FAKE_OUTPUT", "");
        }
    }

    EngineResult correct(std::string_view segment) override {
        EngineResult r;
        if (!error_.empty() || mode_ == Mode::Error) {
            r.error = error_;
            r.error_detail = error_detail_;
            return r;
        }
        if (mode_ == Mode::Output) {
            r.text = output_;
            return r;
        }
        const std::string key(trim_like_python(segment));
        const auto it = map_.find(key);
        r.text = it != map_.end() ? it->second : key;
        return r;
    }

private:
    enum class Mode { Error, Map, Output };

    // Any failure (unreadable file, invalid JSON, not an object of strings) => load_failed.
    void load_map(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error_ = "load_failed";
            error_detail_ = "cannot read REVISA_FAKE_MAP: " + path;
            return;
        }
        const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        bool ok = !j.is_discarded() && j.is_object();
        if (ok) {
            for (const auto& [key, value] : j.items()) {
                if (!value.is_string()) {
                    ok = false;
                    break;
                }
                map_.emplace(key, value.get<std::string>());
            }
        }
        if (!ok) {
            map_.clear();
            error_ = "load_failed";
            error_detail_ = "REVISA_FAKE_MAP is not a JSON object of strings: " + path;
        }
    }

    Mode mode_ = Mode::Output;
    std::string error_;  // non-empty => every call fails
    std::string error_detail_;
    std::string output_;
    std::map<std::string, std::string> map_;
};

}  // namespace

std::unique_ptr<Engine> fake_engine_from_env() {
    if (std::getenv("REVISA_FAKE_ERROR") == nullptr && std::getenv("REVISA_FAKE_MAP") == nullptr &&
        std::getenv("REVISA_FAKE_OUTPUT") == nullptr)
        return nullptr;
    return std::make_unique<FakeEngine>();
}

}  // namespace revisa::infer

#else

namespace revisa::infer {

std::unique_ptr<Engine> fake_engine_from_env() { return nullptr; }

}  // namespace revisa::infer

#endif
