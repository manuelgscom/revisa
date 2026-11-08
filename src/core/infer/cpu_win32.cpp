#include "core/infer/cpu.hpp"

#include <cstddef>
#include <set>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <intrin.h>

namespace revisa::infer {

namespace {

// The RelationProcessorCore records (one per physical core, all processor groups); empty on error.
std::vector<unsigned char> core_records() {
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return {};
    std::vector<unsigned char> buf(size);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &size)) return {};
    buf.resize(size);
    return buf;
}

// Calls f(record) for every RelationProcessorCore record.
template <typename F>
void for_each_core(const std::vector<unsigned char>& buf, F f) {
    for (std::size_t offset = 0; offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= buf.size();) {
        const auto* rec = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + offset);
        if (rec->Size == 0) break;
        if (rec->Relationship == RelationProcessorCore) f(*rec);
        offset += rec->Size;
    }
}

}  // namespace

// Physical cores: one RelationProcessorCore record per core (all processor groups).
int physical_cores() {
    const std::vector<unsigned char> buf = core_records();
    int cores = 0;
    for_each_core(buf, [&](const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX&) { ++cores; });
    return cores > 0 ? cores : 1;
}

// Hybrid CPUs: the cores with the highest EfficiencyClass (higher = faster; all 0 on a
// non-hybrid CPU). With a single class, every physical core.
int default_threads() {
    const std::vector<unsigned char> buf = core_records();
    std::set<BYTE> classes;
    for_each_core(buf, [&](const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX& rec) {
        classes.insert(rec.Processor.EfficiencyClass);
    });
    if (classes.size() < 2) return physical_cores();
    const BYTE top = *classes.rbegin();
    int cores = 0;
    for_each_core(buf, [&](const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX& rec) {
        if (rec.Processor.EfficiencyClass == top) ++cores;
    });
    return cores > 0 ? cores : physical_cores();
}

// CPUID: FMA (leaf 1 ECX bit 12), F16C (leaf 1 ECX bit 29), AVX2 (leaf 7 subleaf 0 EBX bit 5),
// and the OS saving the XMM and YMM state (OSXSAVE, leaf 1 ECX bit 27, then XCR0 bits 1 and 2).
bool cpu_supports_release() {
    int regs[4] = {};  // EAX, EBX, ECX, EDX
    __cpuidex(regs, 0, 0);
    const int max_leaf = regs[0];
    if (max_leaf < 7) return false;

    __cpuidex(regs, 1, 0);
    const unsigned ecx1 = static_cast<unsigned>(regs[2]);
    const bool fma = (ecx1 & (1u << 12)) != 0;
    const bool osxsave = (ecx1 & (1u << 27)) != 0;
    const bool f16c = (ecx1 & (1u << 29)) != 0;
    if (!fma || !osxsave || !f16c) return false;
    if ((_xgetbv(0) & 6) != 6) return false;

    __cpuidex(regs, 7, 0);
    const unsigned ebx7 = static_cast<unsigned>(regs[1]);
    return (ebx7 & (1u << 5)) != 0;
}

}  // namespace revisa::infer
