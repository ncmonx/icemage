#pragma once
// filter-v1 runtime: run a sandboxed WASM module over text input.
// Module compiled once + cached by path; instantiated per call in a fresh store
// bounded by fuel/epoch/memory. Never throws — returns false + rerr on failure.
#include "wasm_skill.hpp"
#include "wasm_extractor.hpp"
#include "../graph/extractor/base_extractor.hpp"
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

// Run ext.wasmPath (.wasm or .wat) over source `content` under extractor-v1.
// Exports: memory, icmg_alloc(len)->ptr, icmg_extract(ptr,len)->i64 packed
// (outPtr<<32|outLen). Output is a UTF-8 JSON string mapped into `result`
// (graph::ExtractResult). sha256 pinning + fuel/epoch/memory bounds apply.
// Returns false (rerr set) on any error/trap/malformed-output — never crashes.
bool runWasmExtractor(const WasmExtractor& ext, const std::string& content,
                      const WasmLimits& lim, graph::ExtractResult& result,
                      std::string& rerr);

// True if the loaded wasmtime supports host-caps (linker path is available).
// When false, modules that IMPORT host funcs (icmg.log) cannot instantiate,
// but import-less filter/extractor modules still run via the legacy path.
bool wasmHostCapsAvailable();

// Test/diagnostic: run a filter module and ALSO capture whatever it wrote via
// the `icmg.log(ptr,len)` host-cap. `hostLog` receives the concatenated
// messages. Behaves exactly like runWasmFilter otherwise.
bool runWasmFilterCapture(const WasmSkill& skill, const std::string& input,
                          const WasmLimits& lim, std::string& out,
                          std::string& hostLog, std::string& rerr);

} // namespace icmg::wasm
