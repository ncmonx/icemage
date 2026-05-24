// v1.31.0 A1.5: cross-platform RAM probe impl. See sys_resources.hpp.
#include "sys_resources.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#  include <mach/mach_host.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>
#  include <unistd.h>
#else
#  include <cstdio>
#  include <unistd.h>
#endif

namespace icmg::core {

namespace {

constexpr std::uint64_t kDefaultMinMB = 1536;  // Qwen 0.5B Q4 + icmg overhead.
constexpr std::uint64_t kHardFloorMB  = 256;   // Refuse override below this.

std::uint64_t parseEnvU64(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return 0;
    char* end = nullptr;
    unsigned long long x = std::strtoull(v, &end, 10);
    if (end == v) return 0;
    return static_cast<std::uint64_t>(x);
}

#if !defined(_WIN32) && !defined(__APPLE__)
// Linux: parse a single key from /proc/meminfo. Returns KB (the file's native unit).
std::uint64_t readMeminfoKB(const char* key) {
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[256];
    std::size_t klen = std::strlen(key);
    std::uint64_t out = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, key, klen) == 0 && line[klen] == ':') {
            // line looks like: "MemAvailable:   12345678 kB"
            const char* p = line + klen + 1;
            while (*p == ' ' || *p == '\t') ++p;
            char* end = nullptr;
            out = std::strtoull(p, &end, 10);
            break;
        }
    }
    std::fclose(f);
    return out;
}
#endif

} // namespace

std::uint64_t availableRamMB() {
#if defined(_WIN32)
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (!GlobalMemoryStatusEx(&st)) return 0;
    return static_cast<std::uint64_t>(st.ullAvailPhys / (1024ull * 1024ull));
#elif defined(__APPLE__)
    vm_size_t page = 0;
    if (host_page_size(mach_host_self(), &page) != KERN_SUCCESS) return 0;
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm), &count) != KERN_SUCCESS)
        return 0;
    std::uint64_t free_bytes = (static_cast<std::uint64_t>(vm.free_count) +
                                static_cast<std::uint64_t>(vm.inactive_count)) *
                               static_cast<std::uint64_t>(page);
    return free_bytes / (1024ull * 1024ull);
#else
    std::uint64_t kb = readMeminfoKB("MemAvailable");
    if (kb == 0) kb = readMeminfoKB("MemFree"); // fallback for older kernels
    return kb / 1024ull;
#endif
}

std::uint64_t totalRamMB() {
#if defined(_WIN32)
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (!GlobalMemoryStatusEx(&st)) return 0;
    return static_cast<std::uint64_t>(st.ullTotalPhys / (1024ull * 1024ull));
#elif defined(__APPLE__)
    int mib[2] = { CTL_HW, HW_MEMSIZE };
    std::uint64_t bytes = 0;
    std::size_t len = sizeof(bytes);
    if (sysctl(mib, 2, &bytes, &len, nullptr, 0) != 0) return 0;
    return bytes / (1024ull * 1024ull);
#else
    return readMeminfoKB("MemTotal") / 1024ull;
#endif
}

std::uint64_t llmMinRamThresholdMB(std::uint64_t model_min_mb) {
    std::uint64_t env = parseEnvU64("ICMG_LLM_MIN_RAM_MB");
    if (env > 0) return std::max(env, kHardFloorMB);
    if (model_min_mb > 0) return std::max(model_min_mb, kHardFloorMB);
    return kDefaultMinMB;
}

bool llmHasEnoughRam(std::uint64_t model_min_mb) {
    std::uint64_t avail = availableRamMB();
    if (avail == 0) return false;            // probe failed -> refuse (fail-closed).
    return avail >= llmMinRamThresholdMB(model_min_mb);
}

} // namespace icmg::core
