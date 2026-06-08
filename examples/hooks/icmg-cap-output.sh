#!/usr/bin/env bash
# PostToolUse hook for Bash: route huge stdout through `icmg shrink` for
# intelligent content-aware shrinking. Falls back to head+tail when icmg
# unavailable.
#
# Install: `.claude/settings.local.json` PostToolUse matcher "Bash".
set -uo pipefail
out=$(jq -r '.tool_response.stdout // .tool_response.output // empty')
sz=${#out}
CAP=${ICMG_CAP_BYTES:-50000}
[[ "$sz" -le "$CAP" ]] && exit 0

# Prefer icmg shrink if available.
if command -v icmg >/dev/null 2>&1; then
    shrunk=$(printf '%s' "$out" | icmg shrink --threshold 0 2>/dev/null)
    if [[ -n "$shrunk" ]]; then
        jq -n --arg m "$shrunk" '{
            hookSpecificOutput: {
                hookEventName: "PostToolUse",
                additionalContext: $m
            }
        }'
        exit 0
    fi
fi

# Fallback: head+tail spill to /tmp.
hash=$(echo -n "$out" | sha1sum | cut -c1-8)
spill="/tmp/icmg-spill-$hash.txt"
printf '%s' "$out" > "$spill"
head_part=$(printf '%s' "$out" | head -c 4096)
tail_part=$(printf '%s' "$out" | tail -c 2048)
msg=$(printf '%s\n... [truncated, %d bytes total, full at %s] ...\n%s' "$head_part" "$sz" "$spill" "$tail_part")
jq -n --arg m "$msg" '{
    hookSpecificOutput: {
        hookEventName: "PostToolUse",
        additionalContext: $m
    }
}'
