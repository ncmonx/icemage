#!/usr/bin/env bash
# PreToolUse:Bash hook — auto-rewrite known noisy commands through `icmg run`.
# Mirrors the pattern rtk-ai uses; lets you type plain `grep`, `node`, `cargo build`
# etc. while still getting Tkil filtering + token recording.
#
# Install: add to .claude/settings.local.json under hooks.PreToolUse with matcher "Bash".
#
# Default-deny + redirect approach: when input matches a covered prefix, deny the
# raw call and tell the agent to retry through `icmg run`. Subsequent attempt is
# allowed (icmg detects loop via env var ICMG_RUN_REWRITE=1 if you wire it).
#
# Bypass: prefix command with `RAW=1 ` to skip rewrite.

set -uo pipefail

INPUT=$(cat)
CMD=$(echo "$INPUT" | jq -r '.tool_input.command // empty' 2>/dev/null)
[[ -z "$CMD" ]] && exit 0

# Bypass marker
echo "$CMD" | grep -qE '^RAW=1 ' && exit 0

# Already through icmg / rtk
echo "$CMD" | grep -qE '(^|[ |&;])(icmg|rtk)[ ]+' && exit 0

# Patterns to redirect (mirror Tkil detector)
PATTERN='^[[:space:]]*(grep|rg|ag|fd|find|node|deno|bun|ts-node|tsx|python|python3|py|ruby|php|java|perl|lua|cargo build|cargo test|cargo check|npm test|npm run build|yarn build|jest|vitest|pytest|dotnet build|dotnet test|dotnet run|go build|go test|go run|cmake|make|ninja|msbuild|gradle build|mvn|sqlcmd|osql|mysql|mariadb|psql|git log|git diff|git show|git status)([[:space:]]|$)'

if echo "$CMD" | grep -qE "$PATTERN"; then
    jq -n --arg c "$CMD" '{
        hookSpecificOutput: {
            hookEventName: "PreToolUse",
            permissionDecision: "deny",
            permissionDecisionReason: ("Use `icmg run " + $c + "` for token-filtered output (60-90% smaller). Bypass with RAW=1 prefix.")
        }
    }'
    exit 2
fi
exit 0
