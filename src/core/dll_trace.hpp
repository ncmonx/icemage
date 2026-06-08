#pragma once
// Runtime DLL-load tracer (Windows). Some err126 ("specified module could not be
// found") crashes come from a module loaded by NAME at runtime (a Vulkan ICD via
// vulkan-1, an OpenSSL/onnx provider, etc.) -- absent from BOTH the static and
// delay import tables, so the PE walk in dll_probe.hpp cannot see it. We register
// an ntdll LDR notification: it fires AFTER each successful DLL load. We remember
// the LAST loaded module so the crash handler can report "died shortly after
// loading X" -> names the subsystem that was initializing (the failed load itself
// gets no notification, but its loader IS the last success). With ICMG_TRACE_DLL=1
// we also stream every load to stderr. No procmon, no admin. POSIX: no-op.
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winternl.h>   // UNICODE_STRING
#endif

namespace icmg::core {

#ifdef _WIN32
// Last DLL base name successfully loaded (best-effort, for crash diagnostics).
inline std::string& lastLoadedDll() { static std::string s; return s; }

namespace detail {
typedef struct _ICMG_LDR_DLL_NOTIFICATION_DATA {
    ULONG Flags;
    const UNICODE_STRING* FullDllName;
    const UNICODE_STRING* BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
} ICMG_LDR_DLL_NOTIFICATION_DATA;

inline std::string narrowOf(const UNICODE_STRING* u) {
    if (!u || !u->Buffer || u->Length == 0) return {};
    int wlen = (int)(u->Length / sizeof(wchar_t));
    int n = WideCharToMultiByte(CP_UTF8, 0, u->Buffer, wlen, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, u->Buffer, wlen, s.data(), n, nullptr, nullptr);
    return s;
}

inline VOID CALLBACK ldrCallback(ULONG reason, const void* data, void* /*ctx*/) {
    // LDR_DLL_NOTIFICATION_REASON_LOADED == 1
    if (reason != 1 || !data) return;
    auto* d = (const ICMG_LDR_DLL_NOTIFICATION_DATA*)data;
    std::string name = narrowOf(d->BaseDllName);
    if (name.empty()) return;
    lastLoadedDll() = name;
    static bool trace = (std::getenv("ICMG_TRACE_DLL") != nullptr);
    if (trace) {
        std::string full = narrowOf(d->FullDllName);
        fprintf(stderr, "[dll] loaded %s%s%s\n", name.c_str(),
                full.empty() ? "" : "  <- ", full.c_str());
        fflush(stderr);
    }
}
}  // namespace detail

// Register the loader-notification callback once at startup. Cheap + safe; if
// ntdll lacks the API (very old Windows) it silently no-ops.
inline void installDllTracer() {
    using Fn = NTSTATUS(NTAPI*)(ULONG, void*, void*, void**);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    if (!nt) return;
    auto reg = (Fn)GetProcAddress(nt, "LdrRegisterDllNotification");
    if (!reg) return;
    static void* cookie = nullptr;
    reg(0, (void*)&detail::ldrCallback, nullptr, &cookie);
}
#else
inline std::string& lastLoadedDll() { static std::string s; return s; }
inline void installDllTracer() {}
#endif

}  // namespace icmg::core
