#include "core/infer/cpu.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <string>
#include <utility>

#include <sched.h>

namespace revisa::infer {

namespace {

bool read_int(const std::string& path, int& out) {
    std::ifstream in(path);
    return static_cast<bool>(in >> out);
}

// A sysfs CPU list ("0-7,16-19"). Returns false if the file is missing or malformed.
bool read_cpu_list(const std::string& path, std::set<int>& cpus) {
    std::ifstream in(path);
    std::string list;
    if (!std::getline(in, list)) return false;
    std::size_t i = 0;
    const auto number = [&](int& out) {
        if (i >= list.size() || list[i] < '0' || list[i] > '9') return false;
        out = 0;
        while (i < list.size() && list[i] >= '0' && list[i] <= '9' && out < CPU_SETSIZE)
            out = out * 10 + (list[i++] - '0');
        return out < CPU_SETSIZE;
    };
    while (i < list.size() && list[i] != '\n') {
        int first = 0, last = 0;
        if (!number(first)) return false;
        last = first;
        if (i < list.size() && list[i] == '-') {
            ++i;
            if (!number(last) || last < first) return false;
        }
        for (int cpu = first; cpu <= last; ++cpu) cpus.insert(cpu);
        if (i < list.size() && list[i] == ',') ++i;
        else break;
    }
    return !cpus.empty();
}

// Physical cores (unique package_id, core_id pairs) among the CPUs in the affinity mask that pass
// keep. 0 if the topology cannot be read.
template <typename Keep>
int count_cores(const cpu_set_t& mask, Keep keep) {
    std::set<std::pair<int, int>> cores;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &mask) || !keep(cpu)) continue;
        const std::string dir = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/";
        int package = 0, core = 0;
        if (!read_int(dir + "physical_package_id", package) || !read_int(dir + "core_id", core)) return 0;
        cores.emplace(package, core);
    }
    return static_cast<int>(cores.size());
}

}  // namespace

// Physical cores (unique package_id, core_id pairs) among the CPUs in the affinity mask. If the
// topology cannot be read, counts the CPUs in the mask.
int physical_cores() {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof(mask), &mask) != 0) return 1;
    const int n = count_cores(mask, [](int) { return true; });
    return std::max(n > 0 ? n : CPU_COUNT(&mask), 1);
}

// Hybrid Intel CPUs expose two PMUs: /sys/devices/cpu_core (P-cores) and /sys/devices/cpu_atom
// (E-cores). With both, the default is the physical P-cores in the affinity mask (P-cores may have
// SMT, hence the core count); otherwise, or if that leaves none, every physical core.
int default_threads() {
    std::set<int> p_cpus, e_cpus;
    if (!read_cpu_list("/sys/devices/cpu_core/cpus", p_cpus) || !read_cpu_list("/sys/devices/cpu_atom/cpus", e_cpus)) {
        return physical_cores();
    }
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof(mask), &mask) != 0) return physical_cores();
    const int n = count_cores(mask, [&](int cpu) { return p_cpus.count(cpu) > 0; });
    return n > 0 ? n : physical_cores();
}

// __builtin_cpu_supports also checks that the OS saves the AVX state (OSXSAVE + XCR0).
bool cpu_supports_release() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma") && __builtin_cpu_supports("f16c");
#else
    return true;  // not x86: the release SIMD set does not apply
#endif
}

}  // namespace revisa::infer
