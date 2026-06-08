#!/usr/bin/env bash
# PreToolUse hook for Read tool — inject summary for large files instead of blocking.
# Install: add to .claude/settings.local.json under hooks.PreToolUse with matcher "Read".
#
# v2 (2026-05-08):
#   - Default threshold raised 30KB -> 60KB (~1000+ lines avg)
#   - additionalContext mode (default) — Read still proceeds; summary appended
#   - Strict mode (ICMG_SHRINK_STRICT=1) — denies Read entirely (old behavior)
#   - ICMG_SHRINK_EXCLUDE pattern (egrep regex) skips matching files entirely
#   - ICMG_SHRINK_INCLUDE pattern restricts to matching files
#
# Env vars:
#   ICMG_SHRINK_THRESHOLD   bytes (default: 60000)
#   ICMG_SHRINK_STRICT      set to 1 to deny instead of inject context
#   ICMG_SHRINK_EXCLUDE     egrep regex; matching files bypass hook entirely
#   ICMG_SHRINK_INCLUDE     egrep regex; if set, only matching files are checked

set -uo pipefail
file=$(jq -r '.tool_input.file_path // empty')
[[ -z "$file" || ! -f "$file" ]] && exit 0

# Skip excluded files (e.g. project source you want full Read on).
if [[ -n "${ICMG_SHRINK_EXCLUDE:-}" ]]; then
    if echo "$file" | grep -Eq "$ICMG_SHRINK_EXCLUDE"; then
        exit 0
    fi
fi
# Restrict to included files when set.
if [[ -n "${ICMG_SHRINK_INCLUDE:-}" ]]; then
    if ! echo "$file" | grep -Eq "$ICMG_SHRINK_INCLUDE"; then
        exit 0
    fi
fi

sz=$(wc -c < "$file" 2>/dev/null || echo 0)
THRESHOLD=${ICMG_SHRINK_THRESHOLD:-60000}

[[ "$sz" -le "$THRESHOLD" ]] && exit 0

summary=$(icmg summarize "$file" --max-lines 80 2>/dev/null || echo "")
[[ -z "$summary" ]] && exit 0

if [[ "${ICMG_SHRINK_STRICT:-0}" == "1" ]]; then
    # Strict: block Read with summary as reason.
    jq -n --arg s "$summary" --arg f "$file" '{
        hookSpecificOutput: {
            hookEventName: "PreToolUse",
            permissionDecision: "deny",
            permissionDecisionReason: ("Large file \($f) — summary below. Use Read with offset/limit for sections, or `icmg context \($f)` for graph view.\n\n" + $s)
        }
    }'
else
    # Soft: inject summary as additional context; Read still proceeds.
    jq -n --arg s "$summary" --arg f "$file" '{
        hookSpecificOutput: {
            hookEventName: "PreToolUse",
            additionalContext: ("Note: \($f) is large. Heuristic summary (use Read offset/limit to drill in):\n\n" + $s)
        }
    }'
fi
