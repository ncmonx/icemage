#!/usr/bin/env bash
# v1.39.0 C: icmg-shell smart entry — classify prompt → local LLM (trivial) or
# forward marker (complex, user routes to Claude Code).
#
# Usage:
#   icmg-shell "what is icmg?"            # local LLM responds (trivial)
#   icmg-shell "implement feature X"      # prints FORWARD marker (complex)
#
# Wire into shell alias for habit:
#   alias ?='icmg-shell'
#   ? "what is git rebase?"
set -euo pipefail

prompt="$*"
if [[ -z "$prompt" ]]; then
    echo "Usage: icmg-shell <prompt>" >&2
    exit 1
fi

# Classify via C2 intent cache (hot-path safe <2ms).
intent_json=$(icmg intent classify "$prompt" 2>/dev/null || echo '{"intent":"default"}')
intent=$(echo "$intent_json" | grep -oE '"intent":"[^"]*"' | head -1 | sed -E 's/.*"intent":"([^"]*)".*/\1/')

case "$intent" in
    trivial)
        echo "[icmg-shell] intent=trivial → local LLM" >&2
        icmg ask --backend=local "$prompt"
        ;;
    debug|code|decision)
        echo "[icmg-shell] intent=$intent → complex; forward to Claude Code" >&2
        echo ""
        echo "===== FORWARD TO CLAUDE CODE ====="
        echo "$prompt"
        echo "==================================="
        echo ""
        echo "Tip: copy prompt above, paste into Claude Code." >&2
        echo "Or run: claude \"$prompt\"" >&2
        ;;
    *)
        # Default: try local first, fall through note if it fails.
        echo "[icmg-shell] intent=default → trying local first" >&2
        if ! icmg ask --backend=local "$prompt" 2>/dev/null; then
            echo "[icmg-shell] local unavailable; copy prompt to Claude Code:" >&2
            echo "$prompt"
        fi
        ;;
esac
