#pragma once
// Phase B.B7 (v0.57.0): hook-runner internals exposed as plain functions.
//
// `runStopHook` / `runPreCompactHook` / `runPostToolUseReadHook` (runners.cpp)
// used to spawn `icmg distill auto`, `icmg compliance check-thinking`, etc. as
// subprocesses. Each fork burned ~50-150ms (icmg.exe cold start). This module
// exposes the underlying logic as direct C++ calls so the runners stay
// in-process — eliminates 4 subprocess forks per Stop event.
//
// Each function is best-effort: never throws, returns count/status, swallows
// errors internally. Same semantics as the CLI commands they replace.

#include <string>

namespace icmg::core::hooks {

// Distill assistant response into per-statement memory_nodes (topic: `auto: …`).
// Returns count of stored entries (0 on too-short input or no matches).
// Mirrors `icmg distill auto --min-len <min_len>`.
int distillAuto(const std::string& text,
                size_t min_len = 200,
                const std::string& tag = "");

// Distill full session transcript into one consolidated `session: <date>` node.
// Returns 1 on store, 0 on no-content.
// Mirrors `icmg distill session`.
int distillSession(const std::string& text,
                   const std::string& tag = "");

// Count thinking-section words in assistant JSON; log violation if > max_words
// AND caveman flag is on. Returns word count (regardless of violation).
// Mirrors `icmg compliance check-thinking --max-words <max_words>`.
int complianceCheckThinking(const std::string& text, int max_words = 80);

// Convert ~/.icmg/strict-denials.jsonl tail (since last offset) into fail
// memory_nodes. Returns number of new entries stored.
// Mirrors `icmg fail sync-denials`.
int failSyncDenials();

// Reset per-turn tool counter (~/.icmg/tool-counter.txt). Best-effort.
// Mirrors `icmg tool-budget reset`.
void toolBudgetReset();

// Compress text payload; returns compressed string ("" if no-op / below
// threshold / on error). Mirrors `icmg compress --threshold <threshold>`
// with stdin = input, stdout = output.
std::string compressInPlace(const std::string& input, int threshold = 256);

} // namespace icmg::core::hooks
