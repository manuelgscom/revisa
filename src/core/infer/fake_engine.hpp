#pragma once
// Engine driven by environment variables, for tests (only reachable with REVISA_TEST_HOOKS).
// Precedence, first set wins:
//   REVISA_FAKE_ERROR    every call fails with this error code; REVISA_FAKE_ERROR_DETAIL, its detail
//   REVISA_FAKE_MAP      path to a JSON object {"segment": "output", ...}; the key is the trimmed
//                        segment; segments not in the map come back unchanged
//   REVISA_FAKE_OUTPUT   every call returns this text
// An unreadable or invalid REVISA_FAKE_MAP makes every call fail with "load_failed".

#include <memory>

#include "core/infer/engine.hpp"

namespace revisa::infer {

// nullptr unless REVISA_FAKE_ERROR, REVISA_FAKE_MAP or REVISA_FAKE_OUTPUT is set.
std::unique_ptr<Engine> fake_engine_from_env();

}  // namespace revisa::infer
