// core/infer/cpu: thread counts and the release SIMD check.
#include "doctest.h"

#include "core/infer/cpu.hpp"

TEST_CASE("cpu: thread counts are at least 1 and the default never exceeds the physical cores") {
    const int cores = revisa::infer::physical_cores();
    CHECK(cores >= 1);
    const int threads = revisa::infer::default_threads();
    CHECK(threads >= 1);
    CHECK(threads <= cores);
}

TEST_CASE("cpu: the machine running the tests has the release SIMD set (AVX2, FMA, F16C)") {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
    // Development and CI machines run the model tests with ggml built for that set, so they have it.
    CHECK(revisa::infer::cpu_supports_release());
#endif
}
