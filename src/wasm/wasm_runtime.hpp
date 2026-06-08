#pragma once
// filter-v1 runtime: run a sandboxed WASM module over text input.
// Module compiled once + cached by path; instantiated per call in a fresh store
// bounded by fuel/epoch/memory. Never throws — returns false + rerr on failure.
#include "wasm_skill.hpp"
#include <cstdint>
#include <string>

namespace icmg::wasm {

struct WasmLimits {
    uint64_t fuel        = 50'000'000;     // instructions
    int      timeoutMs   = 200;            // wall-clock (epoch interruption)
    size_t   maxOutBytes = 4ull*1024*1024; // clamp filter output
};

// True if wasmtime.dll loaded + all needed symbols resolved (cached).
bool wasmRuntimeAvailable(std::string& err);

// Run skill.wasmPath (.wasm or .wat) over `input` under filter-v1.
// If skill.sha256 is non-empty it must match the file's sha256 (else refuse).
// Returns false (rerr set) on any error/trap — never crashes.
bool runWasmFilter(const WasmSkill& skill, const std::string& input,
                   const WasmLimits& lim, std::string& out, std::string& rerr);

} // namespace icmg::wasm
