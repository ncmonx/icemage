#include "win_api_utf8.hpp"

#ifdef _WIN32
#  include "win_utf8.hpp"
#  include <shellapi.h>
#  include <vector>

namespace icmg::core {

namespace {
// Path buffers start here and double up to the NT long-path limit.
constexpr DWORD kModulePathStart = 512;
constexpr DWORD kModulePathMax   = 32768;

// Owns the wide copy of a nullable UTF-8 C string.
struct WideArg {
    std::wstring s;
    bool present = false;
    explicit WideArg(const char* p) : present(p != nullptr) { if (p) s = utf8ToWide(p); }
    const wchar_t* get() const { return present ? s.c_str() : nullptr; }
};
}  // namespace

BOOL createProcessUtf8(const char* app, char* cmdline,
                       LPSECURITY_ATTRIBUTES procAttrs,
                       LPSECURITY_ATTRIBUTES threadAttrs, BOOL inheritHandles,
                       DWORD flags, LPVOID env, const char* cwd,
                       const STARTUPINFOA* si, PROCESS_INFORMATION* pi) {
    WideArg wapp(app), wcwd(cwd);
    WideArg desktop(si ? si->lpDesktop : nullptr), title(si ? si->lpTitle : nullptr);

    // CreateProcessW may write into lpCommandLine, so it must be mutable.
    std::vector<wchar_t> wcmd;
    if (cmdline) {
        const std::wstring w = utf8ToWide(cmdline);
        wcmd.assign(w.begin(), w.end());
        wcmd.push_back(L'\0');
    }

    STARTUPINFOW siw{};
    siw.cb = sizeof(siw);
    if (si) {
        siw.lpDesktop       = const_cast<wchar_t*>(desktop.get());
        siw.lpTitle         = const_cast<wchar_t*>(title.get());
        siw.dwX             = si->dwX;
        siw.dwY             = si->dwY;
        siw.dwXSize         = si->dwXSize;
        siw.dwYSize         = si->dwYSize;
        siw.dwXCountChars   = si->dwXCountChars;
        siw.dwYCountChars   = si->dwYCountChars;
        siw.dwFillAttribute = si->dwFillAttribute;
        siw.dwFlags         = si->dwFlags;
        siw.wShowWindow     = si->wShowWindow;
        siw.hStdInput       = si->hStdInput;
        siw.hStdOutput      = si->hStdOutput;
        siw.hStdError       = si->hStdError;
    }
    return CreateProcessW(wapp.get(), cmdline ? wcmd.data() : nullptr,
                          procAttrs, threadAttrs, inheritHandles, flags, env,
                          wcwd.get(), &siw, pi);
}

std::string moduleFileNameUtf8(HMODULE module) {
    for (DWORD cap = kModulePathStart; cap <= kModulePathMax; cap *= 2) {
        std::wstring buf(cap, L'\0');
        const DWORD n = GetModuleFileNameW(module, buf.data(), cap);
        if (n == 0) return {};
        if (n < cap) { buf.resize(n); return wideToUtf8(buf); }
        // n == cap: truncated, retry with a bigger buffer.
    }
    return {};
}

bool shellExecuteUtf8(const char* verb, const std::string& target,
                      const char* params, int show) {
    WideArg wverb(verb), wparams(params);
    const std::wstring wtarget = utf8ToWide(target);
    HINSTANCE r = ShellExecuteW(nullptr, wverb.get(), wtarget.c_str(),
                                wparams.get(), nullptr, show);
    return reinterpret_cast<INT_PTR>(r) > 32;
}

}  // namespace icmg::core
#endif
