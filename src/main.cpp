#include <cstdio>

#include "cli/cli.hpp"
#include "core/infer/cpu.hpp"

int main(int argc, char** argv) {
    // The release forces AVX2 + FMA + F16C: without them the first inference would die with an
    // illegal instruction, so refuse to start with a clear message instead.
    if (!revisa::infer::cpu_supports_release()) {
        std::fputs("revisa: este procesador no tiene AVX2 (con FMA y F16C), que revisa necesita.\n", stderr);
        return revisa::cli::kExitError;
    }
    return revisa::cli::run(argc, argv);
}
