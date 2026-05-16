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

// v1.1.0 Task 6: PreToolUse hard-deny enforcement.
// Inspects a single tool invocation against icmg-first rules. Returns
// Claude Code hook v2 JSON ("permissionDecision": "allow"/"deny"/"ask")
// or empty string when bypassed (env ICMG_STRICT_BYPASS=1).
//
// stdin_raw is the raw JSON Claude Code passes to the PreToolUse hook:
//   {"tool_name":"Bash","tool_input":{"command":"cat foo.cpp"}}
//   {"tool_name":"Read","tool_input":{"file_path":"foo.cpp"}}
std::string runPreToolUseEnforce(const std::string& stdin_raw);

// v1.1.0 Task 6.6: caveman per-prompt re-inject + escalation.
// Returns an additionalContext text block reminding the model of caveman
// ultra; intensity scales with violation count in the last 24h.
// Empty when caveman.flag absent or ICMG_CAVEMAN_QUIET=1.
std::string runUserPromptCavemanInject();

// v1.3.0 Task 8: Focus Chain per-prompt re-inject.
// Returns a markdown block of in-progress todos for the current session
// (up to `limit` items). Empty when no in-progress items or ICMG_FOCUS_QUIET=1.
// session_id defaults to ICMG_SESSION_ID env var, then "default".
std::string runFocusChainInject(const std::string& session_id = "", int limit = 5);

// v1.3.0 Task 7: UserPromptSubmit auto-inject top skill chunk.
// Searches skill_chunks in the project DB for the highest-scoring chunk
// matching the user prompt (hybrid BM25+cosine, alpha=0.5).
// Returns a markdown hint block (≤600 chars) when the top score ≥ 0.20,
// empty string otherwise. Fail-soft: any error → "".
// Opt-out: set env ICMG_SKILL_QUIET=1.
std::string runUserPromptSkillSuggest(const std::string& user_prompt);

} // namespace icmg::core::hooks
