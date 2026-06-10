#pragma once
// Runtime Vulkan-ICD gate for the local LLM (ggml) backend.
//
// On a headless Windows Server with no GPU / no Vulkan ICD registered under
// HKLM\SOFTWARE\Khronos\Vulkan\Drivers, constructing a LlamaRunner runs
// llama_backend_init() -> ggml Vulkan backend -> vkCreateInstance, whose
// no-driver failure surfaces as ERROR_MOD_NOT_FOUND (err126) and crashes the
// process (Win Server 2019 17763, confirmed via Procmon — known-issue #32877).
// The shipped Windows build is Vulkan-enabled (GGML_VULKAN=ON), so a compile
// flag cannot help at runtime. Instead we gate the llama backend OFF when no
// ICD is present, so `icmg context` (and any local-LLM path) skip backend init
// entirely — a headless server has no GPU anyway. No init, no probe, no crash.
//
// The decision core is pure (unit-testable); the ICD probe + env reads are thin.
#include <cstdlib>
#include <cstring>

namespace icmg::llm {

// Pure decision: is it safe to initialize the llama/ggml backend?
//   isWindows        — platform (the crash is Windows-headless-specific)
//   icdPresent       — a Vulkan ICD is registered (irrelevant off-Windows)
//   envForceNoVulkan — user set ICMG_GGML_NO_VULKAN=1 / GGML_VULKAN=0
//   envForceVulkan   — user set ICMG_FORCE_VULKAN=1 (escape hatch if the
//                      registry probe false-negatives on a real driver)
// Force-on wins over force-off. Off Windows: always safe. On Windows: safe iff
// an ICD is present.
inline bool llamaBackendSafe(bool isWindows, bool icdPresent,
                             bool envForceNoVulkan, bool envForceVulkan) {
    if (envForceVulkan)   return true;
    if (envForceNoVulkan) return false;
    if (!isWindows)       return true;
    return icdPresent;
}

// True when the user explicitly disabled the Vulkan/ggml-GPU path.
inline bool envForceNoVulkan() {
    if (const char* e = std::getenv("ICMG_GGML_NO_VULKAN")) {
        if (e[0] && std::strcmp(e, "0") != 0) return true;
    }
    if (const char* g = std::getenv("GGML_VULKAN")) {
        if (std::strcmp(g, "0") == 0) return true;
    }
    return false;
}

// True when the user force-enables Vulkan despite a negative ICD probe.
inline bool envForceVulkan() {
    if (const char* e = std::getenv("ICMG_FORCE_VULKAN")) {
        if (e[0] && std::strcmp(e, "0") != 0) return true;
    }
    return false;
}

// Is a Vulkan ICD registered on this host? (Windows: registry probe; else true.)
bool vulkanIcdPresent();

// Combined real-world gate, computed once and cached. Callers (LlamaRunner ctor
// + available()) use this to skip the local-LLM backend on unsafe hosts.
bool localLlmBackendSafe();

} // namespace icmg::llm
