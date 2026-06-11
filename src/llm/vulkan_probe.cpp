// Vulkan-ICD probe — Windows registry read + cached gate. See vulkan_probe.hpp.
#include "vulkan_probe.hpp"

namespace icmg::llm {

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// A Vulkan ICD is advertised as a value under HKLM\...\Khronos\Vulkan\Drivers.
// The key exists with >=1 value when a driver is installed; on a headless
// Server with only the Basic Display Adapter the key is absent/empty.
static bool icdKeyHasValues(const wchar_t* sub) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &h) != ERROR_SUCCESS)
        return false;
    DWORD nvals = 0;
    LONG rc = RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, &nvals, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(h);
    return rc == ERROR_SUCCESS && nvals > 0;
}

bool vulkanIcdPresent() {
    return icdKeyHasValues(L"SOFTWARE\\Khronos\\Vulkan\\Drivers")
        || icdKeyHasValues(L"SOFTWARE\\WOW6432Node\\Khronos\\Vulkan\\Drivers");
}
#else
bool vulkanIcdPresent() { return true; }
#endif

bool localLlmBackendSafe() {
    static const bool safe = [] {
#ifdef _WIN32
        const bool isWin = true;
#else
        const bool isWin = false;
#endif
        return llamaBackendSafe(isWin, vulkanIcdPresent(),
                                envForceNoVulkan(), envForceVulkan());
    }();
    return safe;
}

} // namespace icmg::llm
