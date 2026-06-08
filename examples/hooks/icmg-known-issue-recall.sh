#!/usr/bin/env bash
# PostToolUseFailure hook — when a Bash command errors, look up past resolution.
set -uo pipefail
err=$(jq -r '.tool_response.stderr // empty')
[[ -z "$err" ]] && exit 0
match=$(icmg known-issue match "$err" 2>/dev/null | head -20)
[[ -z "$match" ]] && exit 0
jq -n --arg m "$match" '{
    hookSpecificOutput: {
        hookEventName: "PostToolUseFailure",
        additionalContext: ("Past resolution found:\n" + $m)
    }
}'
