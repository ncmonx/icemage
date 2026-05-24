// v1.31.0 A1.5: cross-platform system resource probes.
//
// Used by LlamaRunner::load() + `icmg llm install/use` to refuse LLM
// activation when host RAM is below the per-model threshold (default
// 1536 MB for Qwen 0.5B Q4). Called ONLY at load/install entry —
// never in hot path. Single syscall, ~1 ms cost.
//
// Override threshold via env ICMG_LLM_MIN_RAM_MB. Hard floor 256 MB
// (any override below is rejected — guards against swap-thrash on
// shared servers).
#pragma once
#include <cstdint>

namespace icmg::core {

// Returns megabytes of physical memory currently available to userland.
// 0 = probe failed (caller should fail-open OR refuse — see callers).
std::uint64_t availableRamMB();

// Total physical RAM in MB. 0 on failure.
std::uint64_t totalRamMB();

// Returns the effective LLM min-RAM threshold in MB.
// Resolution order:
//   1. env ICMG_LLM_MIN_RAM_MB (clamped to >=256)
//   2. per-model override (caller passes model_min_mb, 0 = unset)
//   3. default 1536 MB
std::uint64_t llmMinRamThresholdMB(std::uint64_t model_min_mb = 0);

// Convenience: true if availableRamMB() >= llmMinRamThresholdMB(model_min_mb).
bool llmHasEnoughRam(std::uint64_t model_min_mb = 0);

} // namespace icmg::core
