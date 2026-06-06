#pragma once
// presence_hook.hpp — me-everywhere heartbeat hook template (shared, testable).
//
// `icmg init` writes this script to .claude/hooks/icmg-presence-heartbeat.sh and
// registers it on UserPromptSubmit. Each prompt the session beats its presence to
// the shared machine-local wire (%APPDATA%/icmg or ~/.icmg) so parallel sessions
// see each other. IDENTITY-AGNOSTIC by rule (embedded hooks must not hardcode any
// user/agent identity — they key off the host-provided session_id only).
#include <string>

namespace icmg::cli {

// The hook script body. Reads the UserPromptSubmit JSON on stdin, derives a stable
// session id (host session_id, else pid fallback), beats presence, and emits the
// one-line "[me-everywhere] N other live session(s)" summary back into context.
inline const char* PRESENCE_HEARTBEAT_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. me-everywhere live mutual-awareness across
# parallel sessions on this machine. Identity-agnostic: keys off session_id only.
command -v icmg >/dev/null 2>&1 || exit 0
INPUT=$(cat)
SID=$(printf '%s' "$INPUT" | icmg hookio get session_id 2>/dev/null)
[ -z "$SID" ] && SID="sess-$$"
MSG=$(ICMG_SESSION_ID="$SID" icmg presence sync --focus active 2>/dev/null)
[ -n "$MSG" ] && printf '%s' "$MSG" | icmg hookio emit UserPromptSubmit --ctx-stdin
exit 0
)BASH";

// The settings.local.json UserPromptSubmit command that runs the script.
// Guarded so it is a fast no-op when the script is absent.
inline std::string presenceHeartbeatHookCmd() {
    return "bash -c '[ -f .claude/hooks/icmg-presence-heartbeat.sh ] && "
           "bash .claude/hooks/icmg-presence-heartbeat.sh || exit 0'";
}

}  // namespace icmg::cli
