// Vulkan-ICD probe — Windows registry read + cached gate. See vulkan_probe.hpp.
#include "vulkan_probe.hpp"

#include <filesystem>
#include <string>

namespace icmg::llm {

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// A Vulkan ICD is advertised as a value under HKLM\...\Khronos\Vulkan\Drivers;
// the value NAME is the full path of the driver's JSON manifest. Collect the
// names so presence can be verified on disk — a stale entry (uninstalled
// driver) keeps the value while the file is gone, which false-positived the
// old "key has >=1 value" probe and let vkCreateInstance crash err126 on
// Windows Server (issue recurrence 2026-07-02, host already on the gate).
static void collectIcdManifests(const wchar_t* sub, std::vector<std::wstring>& out) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &h) != ERROR_SUCCESS)
        return;
    wchar_t name[1024];
    for (DWORD i = 0;; ++i) {
        DWORD nlen = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
        LONG rc = RegEnumValueW(h, i, name, &nlen, nullptr, nullptr, nullptr, nullptr);
        if (rc != ERROR_SUCCESS) break;   // ERROR_NO_MORE_ITEMS or read failure
        out.emplace_back(name, nlen);
    }
    RegCloseKey(h);
}

bool vulkanIcdPresent() {
    std::vector<std::wstring> manifests;
    collectIcdManifests(L"SOFTWARE\\Khronos\\Vulkan\\Drivers", manifests);
    collectIcdManifests(L"SOFTWARE\\WOW6432Node\\Khronos\\Vulkan\\Drivers", manifests);
    return anyIcdManifestPresent(manifests, [](const std::wstring& p) {
        std::error_code ec;                          // no-throw exists (err126-safe)
        return std::filesystem::exists(std::filesystem::path(p), ec);
    });
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
