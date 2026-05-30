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

// v1.21.4 (X1): PreCompact per-snippet preservation. Scans transcript for
// individual Decision/Fix/Root cause/IMPORTANT/Conclusion/Workaround/TODO
// statements, stores each as its own memory node (topic
// `auto:precompact-<date>-<idx>`). Higher cap (30) than distillAuto's 8.
// Returns count stored. Opt-out: ICMG_NO_X1_EXTRACT=1.
int extractPreCompactSnippets(const std::string& text);

// v1.21.7 (FB2): record raw transcript into FTS5-indexed `transcripts` table
// before compaction destroys it. Caps content at `max_chars` (default 200 KB).
// Returns 1 on store, 0 on skip/empty. Opt-out: ICMG_NO_TRANSCRIPT_STORE=1.
int recordTranscript(const std::string& session_id,
                     const std::string& text,
                     size_t max_chars = 200000);

// Count thinking-section words in assistant JSON; log violation if > max_words
// AND sayless flag is on. Returns word count (regardless of violation).
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

// v1.1.0 Task 6.6: sayless per-prompt re-inject + escalation.
// Returns an additionalContext text block reminding the model of sayless
// ultra; intensity scales with violation count in the last 24h.
// Empty when sayless.flag absent or ICMG_SAYLESS_QUIET=1.
std::string runUserPromptSaylessInject();

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

// v1.3.0 Task 13: PostToolUse:Bash test-fail auto-context bundle.
// Inspects tool_output for failure signatures (FAIL, error:, Traceback, etc.).
// When detected, extracts candidate file paths from output, queries graph/memory,
// and returns a ≤1.2 KB markdown debug context block for additionalContext.
// Returns "" when no failure detected, on empty input, or on any error.
// Opt-out: set env ICMG_DEBUG_CONTEXT_QUIET=1.
std::string runPostToolUseTestFailContext(const std::string& tool_input_command,
                                          const std::string& tool_output);

// v1.4.0 Task 1: PreToolUse:Edit disambiguation hook.
// Inspects a PreToolUse Edit/Write tool invocation. When the target file_path
// is ambiguous (>=2 context-graph candidates score above threshold), returns
// Claude Code hook v2 JSON with permissionDecision:"ask" and a candidates list.
// Returns empty string when:
//   - Only 0 or 1 candidate above threshold (no ambiguity)
//   - ICMG_DISAMBIG_QUIET=1 env is set
//   - DB unavailable (fail-soft)
//   - Not an Edit/Write tool call
std::string runPreToolUseEditDisambig(const std::string& stdin_raw);

// v1.4.0 Task 2: PreToolUse:Bash git guard.
// Intercepts `git checkout <file>`, `git restore <file>`, `git reset --hard <file>`
// patterns that operate on file paths (not commit hashes or branch names).
// Returns Claude Code hook v2 JSON with permissionDecision:"deny" and a redirect
// message pointing to `icmg safe-rollback <file>`.
// Returns allowJson() (empty string callers treat as allow) when:
//   - Pattern does not match a file-path form
//   - ICMG_GIT_GUARD_QUIET=1 env is set
//   - stdin is empty or malformed
//   - Not a Bash tool call
std::string runPreToolUseBashGitGuard(const std::string& stdin_raw);

// v1.4.0 Task 3: PostToolUse:Edit/Write auto graph-update + memory draft.
// Intercepts PostToolUse Edit/Write events. For each intercepted event:
//   1. Parses file_path, old_string/new_string (Edit) or content (Write).
//   2. Skips paths outside the project root.
//   3. Writes a draft ContextNode (tier="draft") summarising the change.
//   4. Returns "" always (silent side-effect hook — no additionalContext inject).
// Opt-out: set env ICMG_AUTO_SYNC_QUIET=1.
// Fail-soft: empty input → "", parse error → "", DB unavailable → "", no throw.
std::string runPostToolUseEditAutoSync(const std::string& stdin_raw);

// v1.4.0 Task 5: PreToolUse approach-history inject.
// Tokenizes user_prompt (lowercase, split on whitespace+punctuation, drop ≤2 chars).
// For each token, searches the approaches table for matching tasks. If ≥1 match
// found, returns a markdown block (≤600 chars) summarising past outcomes.
// Fail-soft: no DB / parse error → "". Opt-out: ICMG_APPROACH_QUIET=1.
std::string runUserPromptApproachInject(const std::string& user_prompt);

// v1.4.0 Task 5: PostToolUse:Bash test-outcome auto-record.
// Detects test runner via pattern on tool_input_command (ctest / npm test /
// pytest / cargo test / go test). When matched and output contains a success or
// failure signature, inserts a row into approaches keyed on the current session's
// first in-progress FocusChain todo. If no focus item or no recognized runner,
// returns "" silently. Opt-out: ICMG_APPROACH_QUIET=1. Always returns "".
std::string runPostToolUseTestOutcome(const std::string& tool_input_command,
                                      const std::string& tool_output,
                                      int exit_code);


// v1.33.0 R6: pinned-rules auto-inject.
// Returns top-N (default 5) active workflow/coding/arch rules from current
// project's rule store, formatted as compact markdown ≤350 chars. Empty
// when no rules or DB unavailable. Opt-out env ICMG_RULE_INJECT_QUIET=1.
// Prevents AI forgetting rules buried in long CLAUDE.md.
std::string runUserPromptPinnedRulesInject(int max_rules = 5);

// v1.33.0 R7: sibling projects auto-inject.
// Returns top-3 sibling projects from `~/.icmg/global.db` projects table,
// ordered by last-touched. Formatted ≤180 chars. Prevents AI losing track
// of user's other active codebases. Opt-out env ICMG_PROJECTS_INJECT_QUIET=1.
std::string runUserPromptProjectsInject();



// v1.34.0 A1: known-issue auto-recall on UserPromptSubmit. Top-2
// errors-resolved% memory_nodes match prompt keywords. ≤180 chars output.
// Opt-out: ICMG_KNOWN_ISSUE_QUIET=1.
std::string runUserPromptKnownIssueInject(const std::string& user_prompt);

// v1.34.0 A2: fail auto-recall. Top-2 fail:* anti-pattern nodes match
// prompt keywords. Helps AI avoid repeating known-bad approaches.
// Opt-out: ICMG_FAIL_INJECT_QUIET=1.
std::string runUserPromptFailInject(const std::string& user_prompt);

// v1.34.0 A3: recent decisions inject. Last 3 [saved] entries from
// session-log.md tail. ≤300 chars compact. Prevents AI forgetting
// what was just decided in prior sessions.
// Opt-out: ICMG_DECISIONS_INJECT_QUIET=1.
std::string runUserPromptRecentDecisionsInject();

// v1.34.0 A4: drift banner. Returns ⚠️ header if any decisions row
// has superseded_at >= now-24h. Empty otherwise.
// Opt-out: ICMG_DRIFT_INJECT_QUIET=1.
std::string runUserPromptDriftInject();

// v1.35.0 R8: auto-pin escalated rules. Top-3 most-violated rule_ids
// (count_total >= 2) with last ctx, prepended to UserPromptSubmit header.
// Opt-out: ICMG_R8_AUTOPIN_QUIET=1.

// v1.38.0 A7: scan last AI response for prior-decision match via BM25 over
// memory_nodes (last 7d). Logs to amnesia_events on hit. Returns hit count.
int runStopAmnesiaScan(const std::string& ai_response);

// v1.38.0 A7 companion: inject "AMNESIA WARNING" header for UserPromptSubmit
// when last 24h has unresolved amnesia_events.
std::string runUserPromptAmnesiaInject();

// v1.38.0 token budget enforce. Returns 1 if prompt estimate exceeds cap
// (default 50k, override via ~/.icmg/token-budget.json), 0 otherwise.
int runPreToolUseTokenBudget(const std::string& prompt);

// v1.38.0 force-compress: returns compressed/capped tool output when size
// exceeds ICMG_FORCE_COMPRESS_KB (default 2 KB). Empty when no-op.
std::string runPostToolForceCompress(const std::string& tool_output);
std::string runUserPromptEscalatedRulesInject();
} // namespace icmg::core::hooks
