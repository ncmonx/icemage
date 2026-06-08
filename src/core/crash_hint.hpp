#pragma once
// Self-diagnosing hint for Windows err126 ("The specified module could not be
// found") crashes. When `icmg --version` works but a heavier command (e.g.
// `icmg context`) crashes err126, the cause is NOT a missing top-level bundled
// DLL -- it is a backend that a bundled DLL lazy-loads at runtime (a Vulkan GPU
// driver via ggml-vulkan, an onnxruntime provider, or an OpenSSL provider). The
// offending module's name lives in the OS loader, not the C++ exception, so we
// cannot print it directly -- but we can tell the user exactly how to capture it.
// Pure + header-only so it is unit-testable in isolation.
#include <string>

namespace icmg::core {

// True if an exception message / OS code looks like a Windows module-load failure.
inline bool isModuleLoadError(const std::string& what, int sysCode = 0) {
    return sysCode == 126
        || what.find("specified module could not be found") != std::string::npos
        || what.find("The specified module") != std::string::npos;
}

// Actionable hint for a module-load (err126) crash, or empty string if the
// exception does not look like one. ASCII-only (Windows console safe).
inline std::string moduleLoadHint(const std::string& what, int sysCode = 0) {
    if (!isModuleLoadError(what, sysCode)) return std::string();
    return
        "hint: err126 = a runtime module/DLL failed to load. If `icmg --version`\n"
        "      works, it is NOT a missing top-level DLL -- it is a backend that a\n"
        "      bundled DLL lazy-loads. Most likely one of:\n"
        "        - ggml-vulkan.dll -> a Vulkan GPU driver/ICD (headless servers\n"
        "          have none) -- the common cause on Windows Server.\n"
        "        - onnxruntime -> an execution-provider module.\n"
        "        - libcrypto-3-x64 -> an OpenSSL provider module.\n"
        "      Capture the exact file: Process Monitor, filter Result=NAME NOT\n"
        "      FOUND + Process=icmg.exe, re-run the failing command, read the last\n"
        "      .dll line. Then report it so the backend can degrade gracefully.\n";
}

}  // namespace icmg::core
