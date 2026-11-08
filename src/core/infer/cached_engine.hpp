#pragma once
// Sentence cache: an Engine decorator that remembers the model output of
// the last kCacheEntries segments (LRU), in memory only. Key: SHA-256 of (model id, contract
// version, segment). Only outputs with text are kept: errors are always retried.
// Not thread-safe: it lives on the inference thread (core/queue), like the engine it wraps.

#include <cstddef>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "core/infer/engine.hpp"

namespace revisa::infer {

inline constexpr std::size_t kCacheEntries = 4096;

class CachedEngine final : public Engine {
public:
    // inner must outlive this object. model_id: anything that tells models apart (the GGUF path).
    CachedEngine(Engine& inner, std::string model_id, std::size_t capacity = kCacheEntries);

    EngineResult correct(std::string_view segment) override;

    std::size_t size() const { return index_.size(); }
    std::size_t hits() const { return hits_; }

private:
    using Entry = std::pair<std::string, std::string>;  // key, output text

    Engine& inner_;
    std::string model_id_;
    std::size_t capacity_;
    std::list<Entry> lru_;  // most recent first
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
    std::size_t hits_ = 0;
};

}  // namespace revisa::infer
