#!/usr/bin/env bash
# PreToolUse hook for Read tool — replace large-file reads with icmg summarize.
# Install: add to .claude/settings.local.json under hooks.PreToolUse with matcher "Read".

set -uo pipefail
file=$(jq -r '.tool_input.file_path // empty')
[[ -z "$file" || ! -f "$file" ]] && exit 0

sz=$(wc -c < "$file" 2>/dev/null || echo 0)
THRESHOLD=${ICMG_SHRINK_THRESHOLD:-30000}

if [[ "$sz" -gt "$THRESHOLD" ]]; then
    summary=$(icmg summarize "$file" --max-lines 80 2>/dev/null || echo "")
    [[ -z "$summary" ]] && exit 0
    jq -n --arg s "$summary" --arg f "$file" '{
        hookSpecificOutput: {
            hookEventName: "PreToolUse",
            permissionDecision: "deny",
            permissionDecisionReason: ("File >\($f) is large (\($s | length) byte summary). Use Read with offset/limit for specific sections, or icmg context \($f) for graph view.\n\n" + $s)
        }
    }'
fi
