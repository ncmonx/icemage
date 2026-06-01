#pragma once
// Phase B.B3 (v0.56.0): hook event runners as standalone functions.
//
// Both `icmg hook <event>` (hook_cmd.cpp) and the rule-daemon RPC handlers
// (rule_daemon.cpp) call these. Single source of truth — eliminates code
// duplication and lets the daemon path skip the icmg.exe outer fork.
//
// All runners are best-effort: failures swallowed, no exceptions propagated.
// Each returns the JSON response body (empty string = no emit needed).

#include <string>

namespace icmg::core::hooks {

// Stop hook: runs distill/fail-sync/compliance/budget chain.
// Returns "" (no hookSpecificOutput emit needed).
// Honors ICMG_NO_STOP_HOOK=1 opt-out (returns "" immediately).
std::string runStopHook(const std::string& stdin_raw);

// PreCompact hook: snapshot + distill session + emit ABSOLUTE-RULE + pinned anchors.
// Returns JSON string {"hookSpecificOutput": {...}} for caller to emit, or "" on no-op.
// Honors ICMG_NO_PRECOMPACT_HOOK=1.
std::string runPreCompactHook(const std::string& stdin_raw);

// PostToolUse:Read hook: compress large Read output via icmg compress subprocess.
// Returns JSON string {"hookSpecificOutput": {...}} with compressed content if
// applicable; empty string if content too small / compress failed / no-op.
// Honors ICMG_NO_COMPRESS_HOOK=1.
std::string runPostToolUseReadHook(const std::string& stdin_raw);

} // namespace icmg::core::hooks
