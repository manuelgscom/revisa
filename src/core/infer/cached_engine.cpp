#include "core/infer/cached_engine.hpp"

#include "core/fetch/sha256.hpp"

namespace revisa::infer {

CachedEngine::CachedEngine(Engine& inner, std::string model_id, std::size_t capacity)
    : inner_(inner), model_id_(std::move(model_id)), capacity_(capacity == 0 ? 1 : capacity) {}

EngineResult CachedEngine::correct(std::string_view segment) {
    std::string material = model_id_;
    material += '\0';
    material += std::to_string(kContractVersion);
    material += '\0';
    material += segment;
    std::string key = fetch::sha256_hex(material);

    if (const auto it = index_.find(key); it != index_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second);
        ++hits_;
        EngineResult r;
        r.text = it->second->second;
        return r;  // timings all zero: no model work
    }
    EngineResult r = inner_.correct(segment);
    if (r.text) {
        lru_.emplace_front(key, *r.text);
        index_.emplace(std::move(key), lru_.begin());
        if (index_.size() > capacity_) {
            index_.erase(lru_.back().first);
            lru_.pop_back();
        }
    }
    return r;
}

}  // namespace revisa::infer
