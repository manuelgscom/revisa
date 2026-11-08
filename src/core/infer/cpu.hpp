#pragma once
// Number of inference threads. Default: the performance cores on a hybrid CPU (P + E cores, where
// fewer, faster threads decode faster: 8 beat 20 threads on a 20-core desktop CPU), every
// physical core otherwise. The user can change it with --threads or the `threads` key.

namespace revisa::infer {

// Physical cores available to the process (affinity mask), at least 1.
int physical_cores();

// The default number of inference threads (see above), at least 1.
int default_threads();

// true if this CPU (and the OS, for the AVX state) supports the SIMD set the release build forces:
// AVX2, FMA and F16C. Without them the binary would die with an illegal instruction.
bool cpu_supports_release();

}  // namespace revisa::infer
