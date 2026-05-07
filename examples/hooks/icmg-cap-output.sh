#!/usr/bin/env bash
# PostToolUse hook for Bash — spill huge stdout to /tmp and replace with head+tail.
# Install: hooks.PostToolUse with matcher "Bash".
set -uo pipefail
out=$(jq -r '.tool_response.stdout // .tool_response.output // empty')
sz=${#out}
CAP=${ICMG_CAP_BYTES:-8192}
[[ "$sz" -le "$CAP" ]] && exit 0

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
