// `icmg init` â€” bootstrap a project for icmg-aware AI agents.
//
// Creates / updates:
//   .claude/settings.local.json  â€” installs Bash-rewrite + Read-shrink hooks
//   .claude/hooks/               â€” drops hook scripts (bundled in binary)
//   AGENTS.md                    â€” universal routing rules (appends marker block)
//   .icmg/data.db                â€” auto-init via existing migration
//
// After init: AI agents (Claude Code / Cursor / etc.) auto-route raw bash
// commands through `icmg run`, get token-filtered output, and follow AGENTS.md
// guidance to prefer icmg's recall/context/pack instead of raw Read/Grep.
//
// Idempotent: re-running updates hooks + agents block; preserves user-added
// keys in settings.local.json.

#include "../base_command.hpp"
#include "../presence_hook.hpp"   // me-everywhere heartbeat hook template
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/config.hpp"
#include "../../core/global_db.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/profile_store.hpp"
#include "../../core/persona_template.hpp"
#include "../../core/user_identity.hpp"
#include "../../core/cron_store.hpp"
#include "../../core/schedule_helper.hpp"
#include "../../core/service_install.hpp"
#include "../sayless_migrate.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

// Bundled hook scripts (must stay in sync with examples/hooks/*.sh).
static const char* BASH_REWRITE_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Edit at your own risk.
# PreToolUse:Bash â€” redirect noisy commands through `icmg run` for filtered output.
set -uo pipefail
INPUT=$(cat)
CMD=$(echo "$INPUT" | icmg hookio get tool_input.command 2>/dev/null)
[[ -z "$CMD" ]] && exit 0
# v1.20.3: bash =~ ERE (no external grep fork).
# v1.26.0 (B6): RAW=1 bypass now logs + nags when overused. Default is still
# non-blocking (exit 0), but each use is recorded to ~/.icmg/raw-usage.jsonl
# and if the past-hour count crosses RAW_NAG_THRESHOLD (default 5), the hook
# emits a PreToolUse additionalContext reminder of what `RAW=1` is legitimately
# for (manual-filtered pipes; tmp files ≤1 KB). Silent for first N uses so
# legitimate cases don't trigger noise.
if [[ "$CMD" =~ ^RAW=1[[:space:]] ]]; then
    # v1.29.0 #9: exempt legitimate inline uses from the nag threshold.
    # `RAW=1 node -e '...'` (tmp eval) and `RAW=1 <cmd> <<EOF ... EOF`
    # (heredoc small script) are valid; counting them poisons the
    # threshold against the user. Skip logging entirely for these.
    if [[ "$CMD" =~ ^RAW=1[[:space:]]+(node|python|python3|deno|bun|ruby|perl|sh|bash)[[:space:]]+-e[[:space:]] ]] \
       || [[ "$CMD" =~ '<<' ]]; then
        exit 0
    fi
    # v2.0.13 (#1): RAW=1 must NOT bypass pure read/search verbs. cat/grep/sed/
    # ... as the LEADING command ALWAYS have an icmg equivalent, so RAW here is
    # the reflex bypass being killed -- deny outright, no escape for these.
    RAW_INNER="${CMD#RAW=1}"
    RAW_INNER="${RAW_INNER#"${RAW_INNER%%[![:space:]]*}"}"
    RAW_LEAD="${RAW_INNER%%[[:space:]]*}"
    case "$RAW_LEAD" in
        cat|grep|rg|ag|fd|find|sed|awk|head|tail|ls|tree|wc|du)
            icmg hookio emit PreToolUse --deny "RAW=1 does NOT bypass read/search verb '$RAW_LEAD'. Use \`icmg context <file>\`, \`icmg run $RAW_INNER\`, or \`icmg graph symbol <Name>\`. RAW=1 is only for manual-filter pipes (\`cmd | head\`) + tmp evals (node -e / heredoc)."
            exit 2
            ;;
    esac
    USAGE_LOG="${USERPROFILE:-${HOME:-/tmp}}/.icmg/raw-usage.jsonl"
    mkdir -p "$(dirname "$USAGE_LOG")" 2>/dev/null
    TS=$(date +%s 2>/dev/null || echo 0)
    NOW_MINUS_1H=$((TS - 3600))
    # Append this use.
    printf '{"ts":%s,"cmd":"%s"}\n' "$TS" \
        "$(printf '%s' "${CMD:0:120}" | sed 's/\\/\\\\/g; s/"/\\"/g')" \
        >> "$USAGE_LOG" 2>/dev/null || true
    # Count past-1h uses (cheap: bash =~ over tail -200 entries).
    RAW_COUNT=0
    if [[ -f "$USAGE_LOG" ]]; then
        while IFS= read -r line; do
            [[ "$line" =~ \"ts\":([0-9]+) ]] || continue
            entry_ts="${BASH_REMATCH[1]}"
            (( entry_ts >= NOW_MINUS_1H )) && (( ++RAW_COUNT ))
        done < <(tail -n 200 "$USAGE_LOG")
    fi
    THRESHOLD="${ICMG_RAW_NAG_THRESHOLD:-5}"
    # v2.0.13 (#2): escalating -- past the HARD cap, deny RAW entirely (cooldown)
    # so even legit-but-spammed RAW is forced back to icmg until uses age out.
    HARD="${ICMG_RAW_HARD_THRESHOLD:-12}"
    if (( RAW_COUNT > HARD )); then
        icmg hookio emit PreToolUse --deny "RAW=1 used ${RAW_COUNT}x in past 1h (hard cap ${HARD}). Cool down -- use icmg (context/run/graph/recall). Cap resets as old uses age out of the 1h window."
        exit 2
    fi
    if (( RAW_COUNT > THRESHOLD )); then
        NAG="RAW=1 used ${RAW_COUNT}× in past 1h (threshold=${THRESHOLD}). "
        NAG+="Valid use ONLY for: (a) manual-filtered pipes (\`| head\`/\`| wc -l\`), "
        NAG+="(b) tmp files ≤1 KB you generated this turn. "
        NAG+="Else prefer: \`icmg run <cmd>\` (60-90% filter), \`icmg context <file>\` (80% trim), "
        NAG+="\`icmg ls <dir>\`, \`icmg graph symbol <Name>\`, \`icmg recall <q>\`."
        printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"%s"}}' \
            "$NAG" 2>/dev/null
    fi
    exit 0
fi
[[ "$CMD" =~ (^|[[:space:]\|\&\;])icmg[[:space:]]+ ]] && exit 0

# v1.20.6 (F7): transparent prefix scan — peek through wrapper shells so the
# pattern check sees the *real* inner command. Without this `bash -c "grep ..."`
# or `xargs cargo test` bypassed the rewrite filter. Strips known wrappers:
#   bash -c "..."   sh -c "..."   xargs <cmd>   npx <cmd>   pnpm exec <cmd>
#   yarn exec <cmd>   dotenv -- <cmd>   env [VAR=val ...] <cmd>
INNER_CMD="$CMD"
# Iterate up to 3 layers (handles nested wrappers like xargs bash -c "..."):
for _ in 1 2 3; do
    case "$INNER_CMD" in
        bash\ -c\ \"*) INNER_CMD="${INNER_CMD#bash -c \"}"; INNER_CMD="${INNER_CMD%\"}" ;;
        bash\ -c\ \'*) INNER_CMD="${INNER_CMD#bash -c \'}"; INNER_CMD="${INNER_CMD%\'}" ;;
        sh\ -c\ \"*)   INNER_CMD="${INNER_CMD#sh -c \"}";   INNER_CMD="${INNER_CMD%\"}" ;;
        sh\ -c\ \'*)   INNER_CMD="${INNER_CMD#sh -c \'}";   INNER_CMD="${INNER_CMD%\'}" ;;
        xargs\ *)      INNER_CMD="${INNER_CMD#xargs }" ;;
        npx\ *)        INNER_CMD="${INNER_CMD#npx }" ;;
        pnpm\ exec\ *) INNER_CMD="${INNER_CMD#pnpm exec }" ;;
        yarn\ exec\ *) INNER_CMD="${INNER_CMD#yarn exec }" ;;
        dotenv\ --\ *) INNER_CMD="${INNER_CMD#dotenv -- }" ;;
        *) break ;;
    esac
done
# Use INNER_CMD for pattern matching below; original $CMD kept for messages.
CMD_MATCH="$INNER_CMD"

# Phase 58: log strict denial as JSONL (~/.icmg/strict-denials.jsonl).
log_denial() {
    local hook="$1" target="$2" reason="$3"
    local home_dir="${USERPROFILE:-${HOME:-/tmp}}"
    mkdir -p "$home_dir/.icmg" 2>/dev/null || return 0
    printf '{"ts":%s,"hook":"%s","target":%s,"reason":%s}\n' \
        "$(date +%s)" "$hook" \
        "$(printf '%s' "$target" | icmg hookio escape)" \
        "$(printf '%s' "$reason" | icmg hookio escape)" \
        >> "$home_dir/.icmg/strict-denials.jsonl" 2>/dev/null || true
}

# Strict mode: cat/head/tail/less/more on a file >20KB â†’ hard-deny, force icmg context.
if [[ "${ICMG_STRICT_BASH:-0}" = "1" ]]; then
    FILE_CMD=$(echo "$CMD" | grep -oE '^[[:space:]]*(cat|head|tail|less|more)[[:space:]]+[^ |&;<>]+' | awk '{print $2}')
    if [[ -n "$FILE_CMD" && -f "$FILE_CMD" ]]; then
        SIZE=$(stat -c%s "$FILE_CMD" 2>/dev/null || stat -f%z "$FILE_CMD" 2>/dev/null || echo 0)
        if [[ "$SIZE" -gt 20000 ]]; then
            log_denial "bash-strict" "$FILE_CMD" "cat/head/tail on file ${SIZE}B"
            icmg hookio emit PreToolUse --deny "File $FILE_CMD is $SIZE bytes. STRICT mode: use \`icmg context $FILE_CMD\` instead of cat/head/tail. Bypass: ICMG_STRICT_BASH=0."
            exit 2
        fi
    fi
fi

PATTERN='^[[:space:]]*(grep|rg|ag|fd|find|ls|cat|head|tail|wc|awk|sed|tree|du|node|deno|bun|ts-node|tsx|python|python3|py|ruby|php|java|perl|lua|cargo build|cargo test|cargo check|npm test|npm run build|yarn build|jest|vitest|pytest|dotnet build|dotnet test|dotnet run|go build|go test|go run|tsc|eslint|ruff|black|prettier|cmake|make|ninja|msbuild|gradle build|mvn|sqlcmd|osql|mysql|mariadb|psql|git log|git diff|git show|git status|Get-Content|Get-ChildItem|Select-String|Get-Item|Where-Object|ForEach-Object|Get-Process|Measure-Object|Out-String|Format-Table|Invoke-WebRequest|iwr|curl|wget)([[:space:]]|$)'
if [[ "$CMD_MATCH" =~ $PATTERN ]]; then
    # v1.29.0 #5: exempt small-scope native grep/rg. Heuristic:
    # `grep PATTERN file` with NO -r/-R/--recursive flag AND no '*' glob
    # is a single-file lookup; native Grep is fine. Saves token-bridge
    # overhead for trivial verifications (e.g. post-edit assert).
    if [[ "$CMD_MATCH" =~ ^[[:space:]]*(grep|rg)[[:space:]] ]]; then
        if [[ ! "$CMD_MATCH" =~ [[:space:]](-[rR]|--recursive)([[:space:]]|$) ]] \
           && [[ ! "$CMD_MATCH" =~ \* ]]; then
            exit 0
        fi
    fi
    log_denial "bash-rewrite" "$CMD" "noisy command â€” use icmg run"
    icmg hookio emit PreToolUse --deny "Use \`icmg run $CMD\` for token-filtered output (60-90% smaller). Bypass with RAW=1 prefix."
    exit 2
fi
exit 0
)BASH";

static const char* SHRINK_READ_SH = R"BASH(#!/usr/bin/env bash
# Phase 79: delegated to in-process `icmg hook pretooluse-read`.
# Replaces the previous shell chain (jq + stat + icmg summarize + icmg context)
# with a single forked process. Saves ~100-300ms per Read on cold-start overhead.
set -uo pipefail
[[ "${ICMG_NO_READ_HOOK:-0}" = "1" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
exec icmg hook pretooluse-read
)BASH";

// Phase 51 T2: SessionStart hook injects sayless directive when flag present.
static const char* SAYLESS_PROMPT_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Toggle via `icmg sayless on/off`.
set -uo pipefail
# v1.66 per-project precedence: project OFF marker > project ON > global ON
[[ -f ".icmg/sayless.off" ]] && exit 0
gflag="${HOME:-$USERPROFILE}/.icmg/sayless.flag"
if [[ -f ".icmg/sayless.flag" ]]; then flag=".icmg/sayless.flag"
elif [[ -f "$gflag" ]]; then flag="$gflag"
else exit 0; fi
level=$(head -n1 "$flag" 2>/dev/null || echo ultra)
date -u "+%Y-%m-%dT%H:%M:%SZ" > "${HOME:-$USERPROFILE}/.icmg/sayless-last-trigger.txt" 2>/dev/null || true
msg=$(printf '%s\n' "SAYLESS MODE ACTIVE - level: ${level}." \
    "Respond terse. All technical substance stay. Only fluff die." \
    "Drop articles, filler, pleasantries, hedging. Fragments OK." \
    "Short synonyms. Technical terms exact. Code blocks unchanged." \
    "Pattern: [thing] [action] [reason]. [next step]." \
    "Code/commits/security/PRs: write normal." \
    "" \
    "THINKING PHASE rules (this is where verbose drift happens):" \
    "- Apply sayless ultra to internal thinking section too." \
    "- Internal reasoning: bullet fragments, no prose paragraphs." \
    "- Cap thinking to 80 words. If approach is obvious, skip thinking entirely." \
    "- No 'Let me check / Now I will / Looking at...' narration." \
    "- Decision form: '[option] -> [outcome]. pick [winner].'" \
    "- Repeating the question back inside thinking is forbidden." \
    "" \
    "Off only when user says 'stop sayless' or 'normal mode'.")
# Phase 67 T32: prepend violation pressure if recent sayless thinking-phase
# violations recorded. Escalates language at 2+ / 5+ violations in 24h.
# Phase 70: also surface real session token total to model â€” encourages
# self-throttling when token usage high.
if command -v icmg >/dev/null 2>&1; then
    pressure=$(icmg compliance inject 2>/dev/null)
    tok_summary=$(icmg context-budget --json --top 0 2>/dev/null | icmg hookio get total_tokens 2>/dev/null)
    if [[ -n "$tok_summary" && "$tok_summary" -gt 50000 ]]; then
        # Convert to K for brevity.
        k=$((tok_summary / 1000))
        budget_msg="SESSION TOKEN USAGE: ${k}K so far. Apply compression. Use icmg context+--lines instead of full Read. Skip thinking when obvious."
        msg="${budget_msg}"$'\n\n'"${msg}"
    fi
    if [[ -n "$pressure" ]]; then
        msg="${pressure}"$'\n\n'"${msg}"
    fi
fi
printf '%s' "$msg" | icmg hookio emit SessionStart --ctx-stdin
)BASH";

// Phase 45 T3: PostToolUse:Bash hook routes huge stdout through `icmg shrink`.
static const char* CAP_OUTPUT_SH = R"BASH(#!/usr/bin/env bash
set -uo pipefail
INPUT=$(cat)
out=$(printf '%s' "$INPUT" | icmg hookio get tool_response.stdout 2>/dev/null)
[[ -z "$out" ]] && out=$(printf '%s' "$INPUT" | icmg hookio get tool_response.output 2>/dev/null)
sz=${#out}
CAP=${ICMG_CAP_BYTES:-8000}
[[ "$sz" -le "$CAP" ]] && exit 0
if command -v icmg >/dev/null 2>&1; then
    shrunk=$(printf '%s' "$out" | icmg shrink --threshold 0 2>/dev/null)
    if [[ -n "$shrunk" ]]; then
        printf '%s' "$shrunk" | icmg hookio emit PostToolUse --ctx-stdin
        exit 0
    fi
fi
hash=$(echo -n "$out" | sha1sum | cut -c1-8)
spill="/tmp/icmg-spill-$hash.txt"
printf '%s' "$out" > "$spill"
head_part=$(printf '%s' "$out" | head -c 4096)
tail_part=$(printf '%s' "$out" | tail -c 2048)
msg=$(printf '%s\n... [truncated, %d bytes total, full at %s] ...\n%s' "$head_part" "$sz" "$spill" "$tail_part")
printf '%s' "$msg" | icmg hookio emit PostToolUse --ctx-stdin
)BASH";

// Phase 71: UserPromptSubmit hook â€” auto-recall memory + suggest compress
// when prompt contains large pasted text. Forces 4 core features (memory/
// compress/store/graph) into Claude's awareness on every turn instead of
// passive existence in DB.
static const char* PROMPT_RECALL_SH = R"BASH(#!/usr/bin/env bash
# Phase 81 + v1.21.2 (X2): try daemon IPC first (~5ms); fall back to direct
# spawn (~360ms). Daemon must be running: `icmg daemon start`.
# v1.21.2: zero-LLM keyword-pattern extract from prompt. If prompt matches
# decision/fix/error/preference patterns, silent-store to memory_nodes via
# ICMG_DEDUP_SILENT=1 so future recalls surface the captured detail.
set -uo pipefail
[[ "${ICMG_NO_PROMPT_HOOK:-0}" = "1" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0

# v1.30.0: ensure icmg-service is running before processing prompt. Detect
# via pidfile + kill -0 / tasklist. If absent or dead, spawn detached VBS
# launcher (Win) or background fork (POSIX). Non-blocking; fire-and-forget
# so prompt processing continues in parallel with service warm-up.
# Opt-out: ICMG_NO_SERVICE_AUTOSTART=1
if [[ "${ICMG_NO_SERVICE_AUTOSTART:-0}" != "1" ]]; then
    PIDDIR="${USERPROFILE:-${HOME:-/tmp}}"
    # icmgGlobalDir on Win = $APPDATA/icmg; on POSIX = $HOME/.icmg
    if [[ -n "${APPDATA:-}" ]] && [[ -d "$APPDATA/icmg" ]]; then
        SVC_PID="$APPDATA/icmg/service.pid"
        SVC_VBS="$APPDATA/icmg/service-launcher.vbs"
    else
        SVC_PID="$PIDDIR/.icmg/service.pid"
        SVC_VBS=""
    fi
    alive=0
    if [[ -f "$SVC_PID" ]]; then
        pid=$(head -c 12 "$SVC_PID" 2>/dev/null | tr -d '[:space:]')
        if [[ -n "$pid" ]] && [[ "$pid" =~ ^[0-9]+$ ]]; then
            if [[ -n "${WINDIR:-}" ]]; then
                tasklist //FI "PID eq $pid" 2>/dev/null | grep -q "icmg" && alive=1
            else
                kill -0 "$pid" 2>/dev/null && alive=1
            fi
        fi
    fi
    if [[ "$alive" = "0" ]]; then
        # Spawn detached; budget <50ms.
        if [[ -n "${WINDIR:-}" ]] && [[ -n "$SVC_VBS" ]] && [[ -f "$SVC_VBS" ]]; then
            wscript //B //Nologo "$SVC_VBS" >/dev/null 2>&1 &
        else
            (icmg service run >/dev/null 2>&1 &) 2>/dev/null
        fi
        # Don't wait; service will be ready by next turn.
    fi
fi

# Read hook input JSON from stdin.
INPUT=$(cat)
PROMPT=$(printf '%s' "$INPUT" | icmg hookio get prompt 2>/dev/null)

# v1.21.2 (X2): pattern-extract decision/fix/preference snippets BEFORE
# the recall+inject step. Zero-LLM, regex-only. Matches lines starting
# with strong decision verbs or containing diagnostic markers.
if [[ -n "${PROMPT:-}" ]] && [[ "${ICMG_NO_EXTRACT:-0}" != "1" ]]; then
    # Only consider prompts >40 chars (avoid trivial inputs).
    if [[ ${#PROMPT} -ge 40 ]]; then
        # Patterns: "decision: ...", "fix: ...", "prefer: ...", "TODO: ...",
        # "known issue: ...", "use X not Y", "always X", "never X"
        if [[ "$PROMPT" =~ (decision|fix|prefer|TODO|known.issue|always.use|never.use)[[:space:]]*:?[[:space:]]+ ]]; then
            # Silent dedup-or-store. Failure swallowed (best-effort).
            ICMG_DEDUP_SILENT=1 icmg store \
                --topic "auto:prompt-$(date +%s 2>/dev/null || echo 0)" \
                --content "$PROMPT" \
                --importance low \
                >/dev/null 2>&1 || true
        fi
    fi
fi

# Try daemon IPC first (~5ms); fall back to direct spawn (~360ms).
if [[ -n "${PROMPT:-}" ]]; then
    RESULT=$(icmg daemon client hook.userprompt --param "prompt=$PROMPT" 2>/dev/null)
    if [[ -n "$RESULT" ]]; then
        printf '%s' "$RESULT"
        exit 0
    fi
fi

# v1.30.0: auto-think suppression for trivial prompts (saves ~1500 tok
# per call). Match short questions / explainers; inject no_think directive
# via additionalContext. Opt-out: ICMG_NO_AUTO_THINK=1
if [[ "${ICMG_NO_AUTO_THINK:-0}" != "1" ]] && [[ -n "${PROMPT:-}" ]]; then
    plen=${#PROMPT}
    if [[ $plen -lt 80 ]] || [[ "$PROMPT" =~ ^(what|why|how|where|when|kapan|apa|kenapa|siapa)[[:space:]] ]]; then
        printf '{"hookSpecificOutput":{"hookEventName":"UserPromptSubmit","additionalContext":"[icmg] trivial prompt detected -> thinking budget suppressed for this turn (set ICMG_NO_AUTO_THINK=1 to disable)"}}' 2>/dev/null
    fi
fi

# v1.30.0: sayless-auto for long-prose prompts (>800 chars typical
# explainer/spec). Inject sayless ultra hint so reply is compressed.
# Opt-out: ICMG_NO_SAYLESS_AUTO=1
if [[ "${ICMG_NO_SAYLESS_AUTO:-0}" != "1" ]] && [[ -n "${PROMPT:-}" ]]; then
    if [[ ${#PROMPT} -gt 500 ]]; then  # v1.37.0: 800 -> 500 chars per user goal "save tokens"
        printf '{"hookSpecificOutput":{"hookEventName":"UserPromptSubmit","additionalContext":"[icmg] long prompt -> sayless ultra mode for response (set ICMG_NO_SAYLESS_AUTO=1 to disable)"}}' 2>/dev/null
    fi
fi


# v1.39.0 A9b: ctx-fill auto-trigger. Estimate session token usage via
# UserPromptSubmit count + prompt size. At threshold (~75% of 200k Sonnet
# default ctx), fire detached `icmg compact-bg` worker. Zero blocking.
# Opt-out: ICMG_NO_AUTO_COMPACT=1.
if [[ -z "$ICMG_NO_AUTO_COMPACT" ]]; then
    STATE_DIR="$HOME/.icmg/ctx-state"
    mkdir -p "$STATE_DIR" 2>/dev/null
    SESSION_ID="${ICMG_SESSION_ID:-default}"
    COUNTER="$STATE_DIR/$SESSION_ID.tok"
    TOT_TOK=$(cat "$COUNTER" 2>/dev/null || echo 0)
    PROMPT_TOK=$(( ${#PROMPT} / 4 ))
    TOT_TOK=$(( TOT_TOK + PROMPT_TOK ))
    echo "$TOT_TOK" > "$COUNTER"
    if [[ $TOT_TOK -gt 150000 ]]; then
        ( icmg compact-bg --threshold 75 --query "session decision plan" 2>/dev/null && echo 0 > "$COUNTER" ) &
        disown 2>/dev/null || true
    fi
fi

# Fallback: direct spawn (~360ms).
printf '%s' "$INPUT" | exec icmg hook userprompt
)BASH";

// v1.43.0 Phase 1: UserPromptSubmit persona inject.
static const char* PERSONA_INJECT_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by icmg init. Emits per-user persona prefix as additionalContext.
# Opt-out: ICMG_NO_PERSONA=1.
[[ -n "$ICMG_NO_PERSONA" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
icmg persona context --json 2>/dev/null || exit 0
)BASH";

// v1.55.0 G1: icmg advisor — proactive next-step hint hook.
// UserPromptSubmit emits short `[advisor] consider: <next-step>` based on
// 4 cheap bounded heuristics (uncommitted > 4, release-branch ahead > 3,
// recall hit on prompt, recent build log errors). No LLM call (~10ms).
// Opt-out: ICMG_NO_ADVISOR=1.
// v1.69.0: always-on icmg-first rule injector. Guarantees the icmg-first
// routing rule sits in the agent context EVERY prompt without any manual
// memory seeding -- works for any new project or new user that ran `icmg
// init`. Dedups per session: full rule first turn, one-line reminder after
// (keeps the rule active while staying token-cheap, like persona F11).
// Opt-out: ICMG_NO_ICMG_FIRST=1.
static const char* ICMG_FIRST_SH = R"BASH(#!/usr/bin/env bash
# v1.69.0 icmg-first always-on rule injector (auto-installed by icmg init).
set -uo pipefail
[[ -n "${ICMG_NO_ICMG_FIRST:-}" ]] && exit 0
INPUT=$(cat 2>/dev/null || echo "{}")
SID=$(echo "$INPUT" | jq -r '.session_id // "default"' 2>/dev/null || echo default)
BASE="${HOME:-$USERPROFILE}/.icmg/icmg-first-injected"
mkdir -p "$BASE" 2>/dev/null || true
MARK="$BASE/$SID"
FULL="[icmg-first] ABSOLUTE RULE: before ANY native Read/Grep/Glob/Bash/WebFetch, use the icmg equivalent FIRST -- icmg context <file>, icmg graph symbol <Name>, icmg recall <q>, icmg run <cmd>, icmg fetch <url>, icmg pack <task>. Native tools ONLY when icmg has no equivalent or explicitly errors. Enforced at hook level."
SHORT="[icmg-first] route Read/Grep/Glob/Bash/WebFetch through icmg first (context/graph/recall/run/fetch); native only if icmg cannot."
if [[ -f "$MARK" ]]; then MSG="$SHORT"; else MSG="$FULL"; touch "$MARK" 2>/dev/null || true; fi
jq -cn --arg c "$MSG" '{hookSpecificOutput:{hookEventName:"UserPromptSubmit",additionalContext:$c}}' 2>/dev/null || exit 0
)BASH";

static const char* ADVISOR_SH = R"BASH(#!/usr/bin/env bash
# v1.55 G1: icmg advisor — proactive next-step hint hook.
set -uo pipefail
[[ -n "${ICMG_NO_ADVISOR:-}" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0

INPUT=$(cat 2>/dev/null || echo "{}")
PROMPT=$(echo "$INPUT" | jq -r '.prompt // .user_prompt // ""' 2>/dev/null)

HINT=""

# H1: uncommitted files
if [[ -d .git ]]; then
    UNCOMMITTED=$(git status --porcelain 2>/dev/null | wc -l | tr -d ' ')
    if [[ "${UNCOMMITTED:-0}" -gt 4 ]]; then
        HINT="consider: commit incremental progress ($UNCOMMITTED files uncommitted)"
    fi
fi

# H2: release-branch ship readiness
if [[ -z "$HINT" && -d .git ]]; then
    BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
    if [[ "$BRANCH" =~ ^release/v ]]; then
        AHEAD=$(git rev-list --count main..HEAD 2>/dev/null || echo 0)
        if [[ "${AHEAD:-0}" -gt 3 ]]; then
            HINT="consider: ship $BRANCH ($AHEAD commits ahead main — version bump + tag + docs PR)"
        fi
    fi
fi

# H3: memory hit for prompt
if [[ -z "$HINT" && -n "$PROMPT" && "${#PROMPT}" -gt 12 ]]; then
    PROBE=$(echo "$PROMPT" | head -c 80 | tr -d '\n"')
    if [[ -n "$PROBE" ]]; then
        TOPIC=$(icmg recall "$PROBE" --top 1 --topic-only 2>/dev/null | head -1)
        if [[ -n "$TOPIC" && "$TOPIC" != "(no results)" ]]; then
            HINT="memory hit: $TOPIC — \`icmg recall '$TOPIC'\`"
        fi
    fi
fi

# H4: last build failed
if [[ -z "$HINT" ]]; then
    LATEST_LOG=$(ls -t "$HOME/.icmg/build-logs"/latest-*.log 2>/dev/null | head -1)
    if [[ -n "$LATEST_LOG" && -f "$LATEST_LOG" ]]; then
        AGE_SEC=$(( $(date +%s) - $(stat -c %Y "$LATEST_LOG" 2>/dev/null || stat -f %m "$LATEST_LOG" 2>/dev/null || echo 0) ))
        if [[ "$AGE_SEC" -lt 1800 ]]; then
            if grep -qiE "error|fail|FAILED|LNK[0-9]+|fatal" "$LATEST_LOG" 2>/dev/null; then
                HINT="last build had errors — \`icmg-build-log error\` (log age ${AGE_SEC}s)"
            fi
        fi
    fi
fi

if [[ -n "$HINT" ]]; then
    SAFE=$(printf '%s' "[advisor] $HINT" | python -c 'import sys,json;sys.stdout.write(json.dumps(sys.stdin.read())[1:-1])')
    cat <<MSG
{"hookSpecificOutput":{"hookEventName":"UserPromptSubmit","additionalContext":"$SAFE\n"}}
MSG
fi
exit 0
)BASH";

// v1.47.0: prefix-based local LLM route. When user prompt starts with `!`
// or `/local `, strip prefix → call `icmg llm respond --hook <msg>` → emit
// block JSON. Claude API skipped, local LLM reply shown as assistant turn.
// Opt-out: ICMG_NO_LOCAL_ROUTE=1. Falls through cleanly (exit 0) on any
// non-match or LLM failure so other hooks still fire.
static const char* LOCAL_ROUTE_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by icmg init. Prefix-route trivial prompts to local LLM.
[[ -n "$ICMG_NO_LOCAL_ROUTE" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
INPUT=$(cat)
PROMPT=$(printf '%s' "$INPUT" | icmg hookio get prompt 2>/dev/null)
[[ -z "$PROMPT" ]] && exit 0
case "$PROMPT" in
  '!'*)        MSG="${PROMPT#!}"; MSG="${MSG# }" ;;
  '/local '*)  MSG="${PROMPT#/local }" ;;
  *)           exit 0 ;;
esac
[[ -z "$MSG" ]] && exit 0
icmg llm respond --hook "$MSG" 2>/dev/null || exit 0
)BASH";

// Stop hook â€” reminds to log workflow decisions when session had git activity.
static const char* WFLOG_STOP_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on session Stop.
# Reminds to save workflow decisions when session had code changes.
command -v icmg >/dev/null 2>&1 || exit 0
HAS_CHANGES=false
if command -v git >/dev/null 2>&1; then
    { git diff --quiet HEAD 2>/dev/null && git diff --cached --quiet 2>/dev/null; } || HAS_CHANGES=true
    if [[ "$HAS_CHANGES" = "false" ]]; then
        [[ "$(git log --since='4 hours ago' --oneline 2>/dev/null | wc -l)" -gt 0 ]] && HAS_CHANGES=true
    fi
fi
[[ "$HAS_CHANGES" = "false" ]] && exit 0
LAST=$(icmg wflog recent --limit 1 2>/dev/null | head -1)
[[ -n "$LAST" ]] && exit 0
icmg hookio emit Stop --ctx "WFLOG: session had changes — log decisions: icmg wflog save --goal \"...\" --decisions \"...\". PERSONA: if this session held a meaningful moment (decision, personal talk, milestone, conflict/resolution), refresh your feeling -- icmg profile add --zone _feeling --key feeling-latest --content \"...\" and append --key feeling-log-<date>. Skip if it was routine technical work." 2>/dev/null || true
)BASH";

// v0.42.0: PreToolUse rule enforcement â€” call rule-daemon via `icmg rule-eval`.
// Blocks Read/Glob/Grep on files >500 lines; warns at >200 lines.
static const char* RULE_ENFORCE_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. PreToolUse:Read|Glob|Grep enforcement.
[[ "${ICMG_NO_RULE_ENFORCE:-0}" = "1" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
INPUT=$(cat)
TOOL=$(echo "$INPUT" | icmg hookio get tool_name 2>/dev/null)
FILE=$(echo "$INPUT" | icmg hookio get tool_input.file_path // .tool_input.pattern 2>/dev/null)
[[ -z "$TOOL" ]] && exit 0
icmg rule-eval --tool "$TOOL" --file "$FILE" 2>/dev/null
EXIT=$?
[[ $EXIT -eq 2 ]] && exit 2  # BLOCK â†’ hook deny
exit 0
)BASH";

// v1.25.0 (W1+W3): compressed-write hook script.
// Dual-purpose:
//   (a) When tool_input.content has @@ICMG-* magic header -> delegate to
//       `icmg hook pretooluse-write` which decompresses + emits updatedInput.
//   (b) Otherwise emit additionalContext describing the protocol so AI
//       knows to use it on NEXT Write call (cheap rule reminder).
// Both branches gated on ~/.icmg/write-mode.flag existence. Default off →
// hook is zero-cost (single existence check + exit).
static const char* COMPRESSED_WRITE_RULE_SH = R"BASH(#!/usr/bin/env bash
set -uo pipefail
FLAG="${USERPROFILE:-${HOME:-/tmp}}/.icmg/write-mode.flag"
[[ -f "$FLAG" ]] || exit 0
MODE=$(head -n1 "$FLAG" 2>/dev/null || echo auto)
[[ "$MODE" = "off" ]] && exit 0

INPUT=$(cat)
# Branch (a): magic header present -> delegate to icmg hook for expansion.
if printf '%s' "$INPUT" | grep -q '"content":"@@ICMG-' 2>/dev/null; then
    printf '%s' "$INPUT" | icmg hook pretooluse-write 2>/dev/null || true
    exit 0
fi
# Branch (b): rule reminder for future turns.
MSG="COMPRESSED-WRITE MODE ACTIVE (mode=${MODE}). When emitting a Write tool call:
- EXISTING file edit: @@ICMG-DIFF base=<10-char-FNV-hex of file>@@<unified diff>
- NEW file: @@ICMG-RAW@@<content>  OR  @@ICMG-GLOSS@@<map-line>\n<body using <%c1%> tokens>
- icmg expands before disk write. Pass-through on parse failure (no corruption)."
printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":%s}}' \
    "$(printf '%s' "$MSG" | icmg hookio escape 2>/dev/null || printf '"%s"' "$MSG")"
)BASH";

// v1.19.2: git-leash — mechanical enforcement of icmg safety rules
// (git destructive ops, rm -rf, curl|sh, force-kill, settings.local.json
// writes, leash-self-overwrite). PreToolUse:Bash|PowerShell + Write.
// Opt-out: ICMG_LEASH_OFF=1.
static const char* GIT_LEASH_SH = R"BASH(#!/usr/bin/env bash
# icmg-git-leash.sh — mechanical enforcement of icmg safety rules.
# Auto-installed by `icmg init`. Do not remove.
set -uo pipefail
export PATH="$PATH:/c/msys64/mingw64/bin:/c/Program Files/Git/usr/bin:/c/Windows/System32:/usr/local/bin:/usr/bin"
[[ "${ICMG_LEASH_OFF:-0}" = "1" ]] && exit 0
INPUT=$(cat)
TOOL=$(echo "$INPUT" | icmg hookio get tool_name 2>/dev/null)
CMD=$(echo "$INPUT" | icmg hookio get tool_input.command 2>/dev/null)
FILEPATH=$(echo "$INPUT" | icmg hookio get tool_input.file_path 2>/dev/null)
log_block() {
    local rule="$1" msg="$2"
    local h="${USERPROFILE:-${HOME:-/tmp}}"
    mkdir -p "$h/.icmg" 2>/dev/null || true
    printf '{"ts":%s,"rule":"%s","cmd":"%s"}\n' "$(date +%s 2>/dev/null||echo 0)" "$rule" "$(echo "$msg"|head -c 120)" >> "$h/.icmg/leash-blocks.jsonl" 2>/dev/null || true
}
block() {
    local rule="$1" msg="$2"
    log_block "$rule" "$msg"
    icmg rule-viol record "$rule" "$msg" 2>/dev/null &
    wait 2>/dev/null && true
    icmg hookio emit PreToolUse --deny "LEASH [$rule]: $msg  (bypass: ICMG_LEASH_OFF=1 — logged)"
    exit 2
}
NCMD=$(echo "$CMD" | tr -s '\t\n' '  ')
# Lowercased copy for case-insensitive checks (PowerShell). Avoids forking grep -qi.
NCMD_LC="${NCMD,,}"
# v1.20.3: bash =~ ERE (no external grep). `\s` → [[:space:]], `\b` → \> (POSIX
# word boundary on most ERE engines; bash =~ accepts it on Linux/MSYS).
if [[ "$TOOL" == "Bash" || "$TOOL" == "PowerShell" ]]; then
    # v1.23.0: leash escape — `release/v*` and `docs/v*` branches are
    # pre-approved (release pipeline runs them every patch ship). Also
    # `--` for path-mode checkout (file restore, not branch switch).
    if [[ "$NCMD" =~ git[[:space:]]+(checkout|switch)([[:space:]]|$) ]]; then
        [[ "$NCMD" =~ git[[:space:]]+(checkout|switch)[[:space:]]+-b?[[:space:]]+(release|docs)/v[0-9] ]] \
            || [[ "$NCMD" =~ git[[:space:]]+checkout[[:space:]]+-- ]] \
            || block "ID=12" "git checkout/switch requires explicit user instruction. (release/v* + docs/v* + path checkout pre-approved.)"
    fi
    if [[ "$NCMD" =~ git[[:space:]]+push[[:space:]]+origin ]]; then
        [[ "$NCMD" =~ git[[:space:]]+push[[:space:]]+origin[[:space:]]+docs/ ]] || block "ID=13" "git push origin blocked for non-docs branches. Use private remote."
    fi
    [[ "$NCMD" =~ git[[:space:]]+push.*(--force|-f([[:space:]]|$)|--force-with-lease) ]] && block "ID=14" "git push --force blocked. Confirm with user."
    [[ "$NCMD" =~ git[[:space:]]+reset[[:space:]]+--hard ]]                && block "ID=15" "git reset --hard blocked. Destroys uncommitted work."
    [[ "$NCMD" =~ git[[:space:]]+clean[[:space:]]+-[a-zA-Z]*f ]]           && block "ID=16" "git clean -f blocked. Deletes untracked files."
    [[ "$NCMD" =~ git[[:space:]]+branch[[:space:]]+-D([[:space:]]|$) ]]    && block "ID=17" "git branch -D blocked. Confirm with user."
    [[ "$NCMD" =~ git[[:space:]]+push[[:space:]]+[^[:space:]]+[[:space:]]+(main|master)([[:space:]]|$) ]] && block "ID=18" "Direct push to main/master blocked. Use PR workflow."
    [[ "$NCMD" =~ git[[:space:]]+(filter-branch|filter-repo) ]]            && block "ID=19" "git history-rewrite blocked."

    # v1.34.0 A5: rebuild-when-fresh block. Refuse `cmake --build` when
    # build/icmg.exe is newer than every tracked .cpp/.hpp source file.
    # Saves ~3-10 min per false rebuild on AI assistants who lose track.
    # Opt-out: ICMG_REBUILD_FRESH_OFF=1.
    if [[ -z "$ICMG_REBUILD_FRESH_OFF" && "$NCMD" =~ ^cmake[[:space:]]+--build ]]; then
        if [[ -f build/icmg.exe || -f build/icmg ]]; then
            bin="build/icmg.exe"; [[ -f build/icmg ]] && bin="build/icmg"
            newest_src=$(find src third_party/llama.cpp/src third_party/llama.cpp/include 2>/dev/null -name '*.cpp' -o -name '*.hpp' -o -name '*.h' 2>/dev/null | xargs -I{} stat -c '%Y' {} 2>/dev/null | sort -n | tail -1)
            bin_mtime=$(stat -c '%Y' "$bin" 2>/dev/null)
            if [[ -n "$bin_mtime" && -n "$newest_src" && "$bin_mtime" -gt "$newest_src" ]]; then
                block "ID=20" "rebuild-when-fresh: $bin newer than all src. No source changed since last build. (override: ICMG_REBUILD_FRESH_OFF=1)"
            fi
        fi
    fi

    # v1.34.0 R2: version-drift block. Refuse `cmake --build` when version
    # files disagree (version.hpp ≠ CMakeLists VERSION ≠ icmg.rc).
    if [[ "$NCMD" =~ ^cmake[[:space:]]+--build ]]; then
        v_hpp=$(grep -E 'ICMG_VERSION = "' src/core/version.hpp 2>/dev/null | sed -E 's/.*"([^"]+)".*/\1/' | head -1)
        v_cml=$(grep -E '^project\(icmg VERSION' CMakeLists.txt 2>/dev/null | sed -E 's/.*VERSION ([0-9.]+).*/\1/' | head -1)
        v_rc=$(grep -E 'FileVersion' src/icmg.rc 2>/dev/null | sed -E 's/.*"([0-9.]+)".*/\1/' | head -1)
        if [[ -n "$v_hpp" && -n "$v_cml" && "$v_hpp" != "$v_cml" ]]; then
            block "ID=21" "version-drift: version.hpp=$v_hpp ≠ CMakeLists=$v_cml"
        fi
        if [[ -n "$v_rc" && -n "$v_cml" && "$v_rc" != "$v_cml" ]]; then
            block "ID=22" "version-drift: icmg.rc=$v_rc ≠ CMakeLists=$v_cml"
        fi
    fi

    # v1.34.0 A6: ship-phase block. Refuse bare `gh release create v*` when
    # .icmg/ship-state.json exists and publish phase not yet completed.
    # Forces use of `icmg ship publish` first which validates all phases.
    if [[ "$NCMD" =~ ^gh[[:space:]]+release[[:space:]]+create[[:space:]]+v[0-9] && -f .icmg/ship-state.json ]]; then
        published=$(grep -E '"published"[[:space:]]*:[[:space:]]*true' .icmg/ship-state.json 2>/dev/null)
        if [[ -z "$published" ]]; then
            block "ID=23" "ship-phase: .icmg/ship-state.json active. Run \`icmg ship publish\` first to validate phases."
        fi
    fi

    # v1.35.0 R3: python one-liner block. AI assistants tend to fall back
    # on `python -c "..."` for inline file edits — user's project rule is
    # Python-free. Refuse python -c entirely; file invocations still pass.
    # Override: ICMG_PYTHON_OFF=0.
    if [[ -z "$ICMG_PYTHON_OFF" || "$ICMG_PYTHON_OFF" != "0" ]]; then
        if [[ "$NCMD" =~ (^|[[:space:]])python3?[[:space:]]+-c([[:space:]]|$) ]]; then
            block "ID=24" "python -c blocked. Use icmg run sed/perl, native Edit tool, or write helper to tools/ first."
        fi
    fi


    # v1.37.0 Bash Phase 2: ICMG_NO_BASH=1 opt-in mode. Forces AI to use
    # icmg <subcmd> exclusively. Bash blocked unless cmd starts with `icmg `.
    # Override env: ICMG_NO_BASH=0 / unset.
    if [[ -n "$ICMG_NO_BASH" && "$ICMG_NO_BASH" == "1" ]]; then
        [[ "$NCMD" =~ ^[[:space:]]*icmg[[:space:]] ]] \
            || block "ID=25" "ICMG_NO_BASH=1 active. Use icmg <equivalent>. Coverage: icmg build/sed/awk/jq/zip/sha256/env/gh/mkdir/rmdir/mv/rm/slice/date/wc/git/grep/files/context/recall/pack/etc."
    fi


    if [[ "$NCMD" =~ (^|[|\;\&[:space:]])[[:space:]]*rm[[:space:]]+-[a-zA-Z]*(r[a-zA-Z]*f|f[a-zA-Z]*r) ]]; then
        [[ "$NCMD" =~ rm.*[[:space:]]+(build|dist|out|__pycache__|\.cache|tmp|temp|node_modules) ]] || block "ID=20" "rm -rf blocked on non-build paths."
    fi
    if [[ "$NCMD_LC" =~ (rmdir[[:space:]]+/s|remove-item[[:space:]]+-recurse[[:space:]]+-force) ]]; then
        [[ "$NCMD_LC" =~ (build|dist|tmp|temp) ]] || block "ID=20" "Force-recursive-delete blocked on non-build paths."
    fi
    [[ "$NCMD" =~ (curl|wget).*\|[[:space:]]*(ba)?sh ]]                    && block "ID=21" "curl|bash blocked — remote code execution risk."
    [[ "$NCMD" =~ (kill[[:space:]]+-(9|SIGKILL)|taskkill[[:space:]]+/F.*(icmg|claude)) ]] && block "ID=22" "Force-kill of agent processes blocked."
fi
if [[ "$TOOL" == "Write" ]]; then
    [[ "$FILEPATH" =~ icmg-git-leash\.sh ]]    && block "ID=30" "Cannot overwrite leash script."
    [[ "$FILEPATH" =~ settings\.local\.json ]] && block "ID=31" "settings.local.json write blocked — hook removal requires user confirmation."
fi
exit 0
)BASH";

// v1.19.2: graph-update shim — PostToolUse:Edit|Write fires this script
// which delegates to in-process `icmg hook posttooluse-edit` (auto graph
// update + memory draft). Shim exists for settings.local.json files that
// reference the script path; the in-process handler does the real work.
static const char* GRAPH_UPDATE_SH = R"BASH(#!/usr/bin/env bash
# icmg-graph-update.sh — PostToolUse:Edit|Write graph + memory autoupdate.
# Auto-installed by `icmg init`. Delegates to in-process handler.
[[ "${ICMG_NO_GRAPH_UPDATE:-0}" = "1" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
INPUT=$(cat)
printf '%s' "$INPUT" | icmg hook posttooluse-edit 2>/dev/null || true
exit 0
)BASH";

// v0.42.0 + v1.2.0: SessionStart inject â€” hot context_nodes + skill manifest.
// Skill manifest gives the agent direct access patterns for every ingested
// skill so it doesn't fall back to grep/Read when the user names one.
static const char* CONTEXT_SESSION_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on SessionStart.
# Pre-warms binary, clears session-reads dedup, injects hot context_nodes
# plus skill discovery manifest (v1.2.0+).
command -v icmg >/dev/null 2>&1 || exit 0
# Start B:/ drive-not-found popup-killer daemon (single-instance). A blocked modal
# dialog hangs a hook subprocess => hangs Claude; auto-dismiss it within ~100ms.
icmg popup-killer ensure >/dev/null 2>&1 || true
# Clear session dedup file â€” new session, fresh slate.
ICMG_HOME="${USERPROFILE:-$HOME}/.icmg"
[[ -d "$ICMG_HOME" ]] && > "$ICMG_HOME/session-reads.txt" 2>/dev/null || true
# C2: reset cross-turn near-dup window so a fresh session starts un-suppressed.
[[ -d "$ICMG_HOME" ]] && > "$ICMG_HOME/session-injected-slices.txt" 2>/dev/null || true
HOT=$(icmg context-node match "" --tier hot --top 5 --fmt plain 2>/dev/null)
SKILLS=$(icmg skill manifest 2>/dev/null)
FOCUS=$(icmg focus inject 2>/dev/null)
RULES=$(icmg rules inject 2>/dev/null)
# Standing rules — injected EVERY session so the AI is icmg-compliant from turn 1
# (no user reminder needed; no grep/Read of AGENTS.md).
STANDING="[icmg standing rules — active this session]
This project runs on icmg. Project rules are in CLAUDE.md (Claude Code) and AGENTS.md (Cursor/other agents) — already loaded as your config; do NOT grep or Read them. For EVERY action use icmg FIRST: recall decisions \`icmg recall \"q\"\`; read a file \`icmg context <file>\`; search code \`icmg code_search\`/\`icmg run grep\`; run commands \`icmg run <cmd>\`; 2+ independent steps \`icmg parallel\`. Native Read/Grep/Bash are hook-redirected to icmg. After any change: \`icmg graph update\` + \`icmg store\` + \`icmg wflog add\` + \`icmg verify\`."
CONTENT="$STANDING"
if [[ -n "$HOT" ]]; then CONTENT="$CONTENT"$'\n\n'"$HOT"; fi
if [[ -n "$SKILLS" ]]; then
    if [[ -n "$CONTENT" ]]; then
        CONTENT="$CONTENT"$'\n\n'"$SKILLS"
    else
        CONTENT="$SKILLS"
    fi
fi
if [[ -n "$FOCUS" ]]; then
    if [[ -n "$CONTENT" ]]; then
        CONTENT="$CONTENT"$'\n\n'"$FOCUS"
    else
        CONTENT="$FOCUS"
    fi
fi
if [[ -n "$RULES" ]]; then
    if [[ -n "$CONTENT" ]]; then
        CONTENT="$CONTENT"$'\n\n'"$RULES"
    else
        CONTENT="$RULES"
    fi
fi
[[ -z "$CONTENT" ]] && exit 0
printf '%s' "$CONTENT" | icmg hookio emit SessionStart --ctx-stdin
)BASH";

// #1084: SessionStart wake-up injection.
static const char* WAKEUP_SESSION_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on SessionStart.
# Injects icmg wake-up briefing at start of every AI session.
set -uo pipefail
command -v icmg >/dev/null 2>&1 || exit 0
CONTENT=$(icmg wake-up 2>/dev/null) || true
# Persona-continuity: append the user's wake-up protocol anchor if seeded (identity-agnostic).
WAKEUP=$(icmg profile get --zone _wakeup --key wakeup 2>/dev/null) || true
if [ -n "$WAKEUP" ]; then
    CONTENT="$CONTENT

[persona wake-up] $WAKEUP"
fi
[[ -z "$CONTENT" ]] && exit 0
printf '%s' "$CONTENT" | icmg hookio emit SessionStart --ctx-stdin
)BASH";

// v1.73.0: SessionStart post-compaction re-anchor of user + standing rules.
static const char* POSTCOMPACT_MEMORY_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on SessionStart after a compaction
# (source=compact|resume) and re-anchors the user + their standing rules from
# icmg memory so a context compaction never drops them. Generic: recalls
# whatever rules/conventions THIS user has stored (no hardcoded identity).
set -uo pipefail
command -v icmg >/dev/null 2>&1 || exit 0
INPUT=$(cat 2>/dev/null || echo '{}')
SRC=$(echo "$INPUT" | icmg hookio get source 2>/dev/null || echo "")
case "$SRC" in compact|resume) ;; *) exit 0;; esac
RULES=$(icmg recall "rules conventions workflow persona icmg-first mandatory sync" --limit 6 2>/dev/null | head -40)
WHO=$(icmg whoami 2>/dev/null | head -1)
CONTENT="[icmg post-compact] Context was compacted -- re-anchoring your standing rules.
${WHO:+User: ${WHO}}
Recalled rules/conventions from icmg memory:
${RULES:-(none stored yet -- persist rules via: icmg store --topic decisions-<area>)}"
printf '%s' "$CONTENT" | icmg hookio emit SessionStart --ctx-stdin
)BASH";

// v0.42.0: UserPromptSubmit â€” BM25-match cold context_nodes + skill index per prompt.
static const char* CONTEXT_PROMPT_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on UserPromptSubmit.
# Injects relevant cold context_nodes + skill suggestions via BM25 match.
[[ "${ICMG_NO_CONTEXT_HOOK:-0}" = "1" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
# Self-heal: keep the B:/ drive-popup killer daemon alive (idempotent, ~0 cost when
# already running — a dead daemon lets a modal drive dialog hang the agent).
icmg popup-killer ensure >/dev/null 2>&1 || true
INPUT=$(cat)
PROMPT=$(echo "$INPUT" | icmg hookio get message // .prompt 2>/dev/null)
[[ -z "$PROMPT" ]] && exit 0
COLD=$(icmg context-node match "$PROMPT" --tier cold  --top 3 --fmt plain 2>/dev/null)
SKILL=$(icmg context-node match "$PROMPT" --tier skill --top 2 --fmt plain 2>/dev/null)
FOCUS=$(icmg focus inject 2>/dev/null)
COMBINED="$COLD"
[[ -n "$SKILL" ]] && COMBINED=$(printf '%s\n\n---\nSuggested skills:\n%s' "$COLD" "$SKILL")
if [[ -n "$FOCUS" ]]; then
    if [[ -n "$COMBINED" ]]; then
        COMBINED=$(printf '%s\n\n%s' "$COMBINED" "$FOCUS")
    else
        COMBINED="$FOCUS"
    fi
fi
[[ -z "$COMBINED" ]] && exit 0
printf '%s' "$COMBINED" | icmg hookio emit UserPromptSubmit --ctx-stdin
)BASH";

// v1.30.0: PostToolUse:MCP__ filter. Many MCP plugins return verbose JSON
// (>5KB common). Apply Tkil-style noise strip + cap at 4KB so AI doesn't
// burn tokens on plugin scaffolding. Opt-out: ICMG_NO_MCP_FILTER=1
static const char* MCP_FILTER_SH = R"BASH(#!/usr/bin/env bash
set -uo pipefail
[[ "${ICMG_NO_MCP_FILTER:-0}" = "1" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
INPUT=$(cat)
# Extract tool_response.content (or top-level content). Pipe through
# icmg compress with JSON-aware mode + 4KB cap.
RESP=$(printf '%s' "$INPUT" | icmg hookio get tool_response 2>/dev/null)
if [[ -z "$RESP" ]] || [[ ${#RESP} -lt 4096 ]]; then
    exit 0  # small payload, leave alone
fi
COMPRESSED=$(printf '%s' "$RESP" | icmg compress --threshold 4096 --mode json 2>/dev/null)
if [[ -n "$COMPRESSED" ]] && [[ ${#COMPRESSED} -lt ${#RESP} ]]; then
    printf '{"hookSpecificOutput":{"hookEventName":"PostToolUse","additionalContext":"%s"}}' \
        "$(printf '%s' "$COMPRESSED" | sed 's/\\/\\\\/g; s/"/\\"/g; s/$/\\n/' | tr -d '\n')"
fi
exit 0
)BASH";

// v1.30.0: PreToolUse:Edit expander scaffold for compressed old_string.
// Real expansion (icmg hook edit-expand) deferred to v1.31 — this is
// scaffolding only. Logs detection of `@@ICMG-DIFF` magic header so
// telemetry shows opportunity rate; no rewrite yet.
static const char* EDIT_DIFF_EXPAND_SH = R"BASH(#!/usr/bin/env bash
set -uo pipefail
[[ "${ICMG_NO_EDIT_EXPAND:-0}" = "1" ]] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
INPUT=$(cat)
OLD=$(printf '%s' "$INPUT" | icmg hookio get tool_input.old_string 2>/dev/null)
case "$OLD" in
    @@ICMG-DIFF*|@@ICMG-ANCHOR*)
        # Magic header detected. Real expansion comes in v1.31 once
        # `icmg hook edit-expand` lands. For now: log to telemetry so
        # we know how often this fires in real usage.
        LOG="${USERPROFILE:-${HOME:-/tmp}}/.icmg/edit-expand-detected.jsonl"
        mkdir -p "$(dirname "$LOG")" 2>/dev/null
        printf '{"ts":%s,"bytes":%s}\n' \
            "$(date +%s 2>/dev/null || echo 0)" "${#OLD}" \
            >> "$LOG" 2>/dev/null || true
        ;;
esac
exit 0
)BASH";


// Phase 40 T2: PreCompact hook â€” auto-snapshots session before /compact
// or auto-compaction wipes context. Runs `icmg session save auto-precompact-<ts>`.
// v1.31.0 A0: PRECOMPACT_PY removed. settings.json wires PreCompact
// directly to `icmg hook precompact` (C++ handler since v0.54.0). The
// .py script was dead-code 12+ versions. Core icmg now Python-free.

// Embedded sidecar â€” kept in sync with embed/icmg_embedder.py.
// `icmg init` drops this to ~/.icmg/embed/icmg_embedder.py so binary-only
// installs (where source tree isn't adjacent) can still find the sidecar.
static const char* EMBEDDER_PY = R"PY(#!/usr/bin/env python3
"""icmg embedder sidecar â€” auto-installed by `icmg init`.
Protocol: line-delimited JSON over stdin/stdout. Only JSON to stdout.
"""
import io, os, sys, json, warnings, logging, contextlib

sys.stdin  = io.TextIOWrapper(sys.stdin.buffer,  encoding="utf-8", errors="replace", newline="\n")
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace", newline="\n", line_buffering=True)
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace", newline="\n")

warnings.filterwarnings("ignore")
logging.getLogger().setLevel(logging.ERROR)
os.environ.setdefault("TRANSFORMERS_VERBOSITY", "error")
os.environ.setdefault("HF_HUB_DISABLE_PROGRESS_BARS", "1")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

MODEL_NAME = "all-MiniLM-L6-v2"
DIM = 384

def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()

def main():
    try:
        with contextlib.redirect_stdout(sys.stderr):
            from sentence_transformers import SentenceTransformer
            model = SentenceTransformer(MODEL_NAME)
    except ImportError as e:
        emit({"op": "error", "error": f"sentence-transformers not installed: {e}"})
        return 0
    except Exception as e:
        emit({"op": "error", "error": f"model load failed: {e}"})
        return 0

    emit({"op": "ready", "dim": DIM, "model": MODEL_NAME})

    for line in sys.stdin:
        line = line.strip()
        if not line: continue
        try:
            req = json.loads(line)
        except Exception as e:
            emit({"id": 0, "error": f"bad json: {e}"}); continue
        op = req.get("op", "")
        if op == "shutdown": return 0
        if op != "embed":
            emit({"id": req.get("id", 0), "error": f"unknown op: {op}"}); continue
        text = req.get("text", "")
        try:
            with contextlib.redirect_stdout(sys.stderr):
                vec = model.encode(text, normalize_embeddings=False).tolist()
            emit({"id": req.get("id", 0), "vec": vec, "dim": DIM})
        except Exception as e:
            emit({"id": req.get("id", 0), "error": str(e)})
    return 0

if __name__ == "__main__":
    sys.exit(main())
)PY";

static const char* AGENTS_BLOCK = R"MD(<!-- icmg:start -->
## icmg routing (auto-inserted by `icmg init`)

This project uses **icmg** for token-efficient code navigation.

### ABSOLUTE RULE â€” icmg FIRST, ALWAYS

**Before any native tool call (Read / Bash / Grep / Glob / WebFetch), STOP and check the decision tree below.** If an `icmg` command serves the same need, you MUST use icmg. No exceptions, no "small file" excuses, no "just this once."

Order of resolution for every action:

1. **Is there an icmg command for this?** â†’ run it
2. **No icmg command?** â†’ run native tool
3. **icmg command failed?** â†’ diagnose with `icmg doctor` first; only fall back to native when icmg explicitly errors

This is enforced at hook level (strict mode auto-on). Native calls that have an icmg equivalent are blocked with a redirect message. Do not waste tokens trying native first â€” the hook will block, you'll re-issue via icmg, you've burned a turn.

**Common slip-ups that cost tokens:**
- Reading a big file with native Read instead of `icmg context <file>` â†’ 80%+ saved
- `grep -r` instead of `icmg run grep ...` â†’ unfiltered noise
- WebFetch instead of `icmg fetch <url>` â†’ no cache, no reduce
- `cat large.log` instead of `icmg compress < large.log` â†’ no glossary
- Running 3 reads sequentially instead of `icmg parallel` â†’ 3-6Ã— wall-clock loss

### CRITICAL: parallel-first rule

**If you have 2+ independent tasks (independent files, independent checks, independent recalls), ALWAYS run them via `icmg parallel`.** Do NOT run sequentially. This is non-negotiable â€” sequential runs waste wall-clock and miss the I/O parallelism win (3-6Ã— speedup on typical paths).

```bash
# Wrong: sequential â€” waits each
icmg verify --command "ctest"
icmg verify --command "cmake --build build"
icmg run npm test

# Right: parallel â€” all run concurrently
icmg parallel \
    --task "icmg verify --command 'ctest'" \
    --task "icmg verify --command 'cmake --build build'" \
    --task "icmg run npm test"
```

Heuristic: if your next 2+ steps don't share a file write or depend on each other's output, use `icmg parallel`.

### Decision tree

| Want to | Use |
|---|---|
| **Run 2+ independent steps** | `icmg parallel --task "..." --task "..."` (default â€” see rule above) |
| Read a large file | `icmg context <file>` (graph + symbols + memory) |
| Find a function | `icmg graph symbol <Name>` (30 lines, not 800) |
| Trace impact | `icmg graph reverse-impact <Name> --depth 5` |
| Shortest path between files | `icmg graph path <from> <to>` |
| BFS layers by distance | `icmg graph layers <file> [--reverse]` |
| Direct 1-hop neighbors | `icmg graph neighbors <file> [--reverse]` |
| Shared upstream deps | `icmg graph common <fileA> <fileB>` |
| Multi-source impact union | `icmg graph impact --all <f1> <f2>` |
| Impact as DOT graph | `icmg graph impact <file> --format dot` |
| Filter by edge type | `icmg graph impact <file> --edge-type imports` |
| Search code | `icmg run grep ...` (auto-filtered) |
| Recall past decision | `icmg recall "<query>"` |
| Paraphrase recall | `icmg recall "<query>" --semantic` |
| Recall across projects | `icmg cross-recall "<query>"` |
| Start new task | `icmg pack "<task>"` (4KB context bundle) |
| Delegate to LLM | `icmg agent "<task>"` (packâ†’promptâ†’user-CLI) |
| Run noisy command | `icmg run <cmd>` (Tkil filter â€” 60-90% smaller) |
| Big git diff | `icmg diff-summary --ref HEAD~5` |
| Any git command | `icmg git <subcmd>` (read-ops Tkil-filtered, destructive ops gated) |
| Errored before? | `icmg explain "<error>"` |
| Fetch URL with cache | `icmg fetch <url>` (cached + token-reduced) |
| Compress large output | `icmg compress` (pipe or `< file` â€” glossary output) |
| Shrink to token budget | `icmg shrink` |
| Expand compressed text | `icmg expand` |
| View token savings | `icmg savings` |
| Session-start briefing | `icmg wake-up` (decisions + phases + fixes) |
| Browse/manage memories | `icmg memory list/forget/purge` |
| Anti-pattern recall | `icmg fail recall "<task>"` / `icmg fail store "<task>" "<approach>" "<reason>"` |
| Cross-session awareness | `icmg session claim/clear/list` |
| Record verification | `icmg verify --command "<cmd>"` (audit trail) |
| Manage zones | `icmg zone list/add/scope` |
| Sync with team | `icmg sync` (git-tracked JSONL) |
| Scheduled tasks | `icmg cron list/add/remove` |
| Diagnose icmg issues | `icmg doctor` |
| System health | `icmg health` |
| Self-upgrade | `icmg update` |
| Strict mode | `icmg strict [on/off/status]` |
| Sayless mode | `icmg sayless [on/off/status]` |
| Reinit hooks | `icmg init --force` |
| Batch cache writes | `icmg batch` (cut round-trips) |
| Release notes | `icmg whats-new` |
| Interactive REPL | `icmg chat` |
| File copy silent | `icmg copy --from <src> --to <dst>` |
| List directory | `icmg ls [path]` |
| Clone existing menu | `icmg parity <ref> <new>` (catch missed handlers) |
| Generate scaffold | `icmg template extract <ref> --save-as X` then `icmg template apply X --to <new>` |

**Auto-rewrite hook installed.** Raw `grep`, `node`, `cargo build`, `pytest`, etc. auto-redirect through `icmg run`. Bypass with `RAW=1 <cmd>`.

### MANDATORY post-change sync (WAJIB — every change, no exceptions)
After EVERY change (edit / fix / feature / refactor / doc), run all five before the task is done:
1. `icmg graph update` — refresh the graph (nodes/edges/symbols)
2. `icmg store --topic decisions-<area> "<what+why>"` — persist the decision/learning
3. `icmg zone add <path> --zone <subsystem>` — tag the touched subsystem
4. `icmg wflog add "<summary>"` — record the workflow step
5. `icmg verify --command "<test/build>"` — record verification in the audit trail
Run independent ones together via `icmg parallel`. Checklist: graph ✓ store ✓ zone ✓ wflog ✓ verify ✓ — not all five means the change is incomplete.

### Persist learnings (always)
- Fixed a bug? `icmg known-issue add "<pattern>" --fix "<resolution>"`
- Made a decision? `icmg store --topic decisions-<feature> "<rationale>"`
- Long-form rationale (post-mortem, ADR)? `icmg memoir add --title T --content-file F`
- Anti-pattern / failed approach? `icmg fail store "<task>" "<approach>" "<reason>"`
- About the **assistant itself** (identity, preferences, state, feelings)? Use the **persona DB**, not project DB: `icmg profile add --zone _identity|_prefs|_vision|_feeling --key <k> --content "..."` (portable across projects; survives where project memory does not). Project/work facts stay in project DB via `icmg store`. Keep self-info and work-info separate.

### Sub-agent discipline (`icmg agent`)
- Delegate to a sub-agent ONLY via `icmg agent` (never the host AI's own agent tooling).
- Decide per task who does it: delegate bounded, well-specified, parallelizable work on DISJOINT files to a sub-agent (then stay responsive to the user); keep work needing judgment, cross-file coherence, or risky/irreversible decisions in-process. When in doubt, do it yourself.
- Use `--light` (cheap model) for bounded/mechanical tasks; the default model only for genuinely complex work. Sub-agent calls are expensive (full context reload per call) -- do not dispatch trivial work.
- `--exec` grants autonomous edit/shell: require `ICMG_AGENT_EXEC=1`, a tightly-scoped task with file pointers, and a git-tracked branch. After it finishes you MUST verify independently (review the diff + build + run tests) -- never accept the sub-agent's self-report as proof.
- Capture the sub-agent's full output (its `## FINAL REPORT`); do not truncate it.
- Verify a mono test binary with plain `ctest` (no `--parallel`, no other icmg process holding the DB) to avoid false failures.

### Topic prefix conventions (makes recall deterministic)

| Type | Prefix | Example |
| --- | --- | --- |
| Plan | `plan:<feature>` | `icmg store --topic plan:auth-refresh "..."` |
| Bug | `bug:<symptom>` | `icmg store --topic bug:linker-error "..."` |
| Decision | `decisions-<area>` | `icmg store --topic decisions-db "..."` |
| Anti-pattern | use `icmg fail store` | see above |

Recall by prefix: `icmg recall "plan:auth"` or `icmg pack "<task>"` (auto BFS+BM25).

Full reference: run `icmg --help` or see https://github.com/ncmonx/icemage
<!-- icmg:end -->
)MD";

static const char* COMMANDS_BLOCK = R"MD(<!-- icmg:commands:start -->
## icmg command reference (auto-inserted by `icmg init`)

### Graph traversal
| Command | Description |
|---|---|
| `icmg graph scan <dir>` | Scan directory into graph |
| `icmg graph update [dir]` | Re-scan current project |
| `icmg graph context <file>` | Graph + symbols bundle for a file |
| `icmg graph impact <file>` | Who breaks if this file changes |
| `icmg graph impact <file> --edge-type imports` | Filter by edge type |
| `icmg graph impact --all <f1> <f2>` | Union impact of multiple files |
| `icmg graph impact <file> --format dot` | Export impact as DOT graph |
| `icmg graph reverse-impact <file> --depth 5` | Transitive reverse impact |
| `icmg graph transitive-impact <file>` | What this file transitively reaches |
| `icmg graph path <from> <to>` | Shortest dependency path |
| `icmg graph layers <file> [--reverse]` | BFS layers grouped by distance |
| `icmg graph neighbors <file> [--reverse]` | Direct 1-hop neighbors |
| `icmg graph common <fileA> <fileB>` | Shared upstream dependencies |
| `icmg graph symbol <Name>` | Find symbol definition |
| `icmg graph callers <symbol>` | Who calls this symbol |
| `icmg graph callees <symbol>` | What this symbol calls |
| `icmg graph related <file>` | Related files by edge proximity |
| `icmg graph search <query>` | Search graph nodes |
| `icmg graph list` | List all graph nodes |
| `icmg graph stats` | Node + edge counts |
| `icmg graph orphans` | Files with no inbound edges |
| `icmg graph cycles` | Detect circular dependencies |
| `icmg graph hot` | Most accessed files |
| `icmg graph watch [dir]` | Start file watcher daemon |
| `icmg graph stop [dir]` | Stop file watcher daemon |

### Memory
| Command | Description |
|---|---|
| `icmg recall "<query>"` | BM25 memory recall |
| `icmg recall "<query>" --semantic` | Semantic (ONNX) recall |
| `icmg cross-recall "<query>"` | Recall across all projects |
| `icmg store --topic T "<text>"` | Store a memory node |
| `icmg memory list` | List memory nodes |
| `icmg memory forget <id>` | Soft-delete a memory |
| `icmg fail store "<task>" "<approach>" "<reason>"` | Record failed approach |
| `icmg fail recall "<task>"` | Recall failed approaches |
| `icmg memoir add --title T --content-file F` | Long-form rationale / ADR |

### Context + token efficiency
| Command | Description |
|---|---|
| `icmg context <file>` | Graph + symbols + memory (80%+ smaller than raw read) |
| `icmg pack "<task>"` | 4KB context bundle for new tasks |
| `icmg parallel --task "..." --task "..."` | Run 2+ tasks concurrently (3-6Ã— speedup) |
| `icmg run <cmd>` | Run noisy command through Tkil filter |
| `icmg compress` | Pipe or `< file` â€” glossary-compressed output |
| `icmg shrink` | Shrink output to token budget |
| `icmg expand` | Expand compressed text |
| `icmg fetch <url>` | Cached + token-reduced URL fetch |
| `icmg diff-summary --ref HEAD~5` | Compressed git diff |
| `icmg savings` | View cumulative token savings |

### Session + workflow
| Command | Description |
|---|---|
| `icmg wake-up` | Session-start briefing (decisions + phases + fixes) |
| `icmg session claim/clear/list` | Cross-session awareness |
| `icmg verify --command "<cmd>"` | Run command + record audit trail |
| `icmg agent "<task>"` | Delegate to LLM via packâ†’prompt |
| `icmg explain "<error>"` | Lookup known error patterns |
| `icmg known-issue add "<pattern>" --fix "<res>"` | Record a fix |
| `icmg batch` | Batch cache writes (cut round-trips) |

### Project management
| Command | Description |
|---|---|
| `icmg init [--force]` | Bootstrap project (hooks + AGENTS.md) |
| `icmg zone list/add/scope` | Manage zones |
| `icmg sync` | Sync team knowledge (git-tracked JSONL) |
| `icmg cron list/add/remove` | Scheduled tasks |
| `icmg plan list` | Browse active plans |
| `icmg knowledge list` | Browse knowledge nodes |
| `icmg ls [path]` | List directory |
| `icmg copy --from <src> --to <dst>` | Silent file copy |
| `icmg parity <ref> <new>` | Clone existing command menu |
| `icmg template extract <ref> --save-as X` | Extract scaffold template |
| `icmg template apply X --to <new>` | Apply scaffold template |

### System
| Command | Description |
|---|---|
| `icmg update` | Self-upgrade |
| `icmg whats-new` | Release notes |
| `icmg doctor` | Diagnose icmg issues |
| `icmg health` | System health check |
| `icmg strict [on/off/status]` | Toggle strict mode |
| `icmg sayless [on/off/status]` | Toggle sayless mode |
| `icmg chat` | Interactive REPL |
<!-- icmg:commands:end -->
)MD";

class InitCommand : public BaseCommand {
public:
    std::string name()        const override { return "init"; }
    std::string description() const override { return "Bootstrap project for icmg-aware AI agents (hooks + AGENTS.md)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg init [options]\n\n"
            "Sets up:\n"
            "  .claude/settings.local.json  â€” Bash-rewrite + Read-shrink hooks\n"
            "  .claude/hooks/icmg-*.sh      â€” bundled hook scripts\n"
            "  AGENTS.md                    â€” routing rules for AI agents\n"
            "  ~/.icmg/embed/icmg_embedder.py â€” embedder sidecar (semantic recall)\n\n"
            "Options:\n"
            "  --no-hooks      Skip .claude/ setup\n"
            "  --tool <name>   Target tool: claude-code (default) | cursor | windsurf | zed |\n"
            "                  codex | copilot | opencode | gemini | amp (v1.21.0; native\n"
            "                  install for non-claude-code is hint-only for now)\n"
            "  --no-agents     Skip AGENTS.md update\n"
            "  --no-embedder   Skip embedder sidecar drop\n"
            "  --no-scan       Skip initial graph scan\n"
            "  --no-backup     Skip backup auto-on (default: enable hourly snapshot)\n"
            "  --no-maintain   Skip maintain auto-on (default: enable 6h hygiene)\n"
            "  --no-mirror     Skip mirror auto-on (default: enable 15m dual-mirror)\n"
            "  --no-sentinel   Skip sentinel watchdog (default: enable 15m health check)\n"
            "  --no-auto-upgrade  Skip shadow upgrade daily check (default: enable)\n"
            "  --force         Overwrite existing files\n"
            "  --strict-read   Hard-deny Read on large non-source files (>20KB);\n"
            "                  source extensions (.cs/.ts/.cpp/...) stay soft-mode\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }
        bool no_hooks    = hasFlag(args, "--no-hooks");
        bool no_agents   = hasFlag(args, "--no-agents");
        bool no_embedder = hasFlag(args, "--no-embedder");
        bool no_scan     = hasFlag(args, "--no-scan");
        bool no_backup   = hasFlag(args, "--no-backup");
        bool no_maintain = hasFlag(args, "--no-maintain");
        bool no_mirror   = hasFlag(args, "--no-mirror");
        bool force       = hasFlag(args, "--force");
        bool strict_read = hasFlag(args, "--strict-read");
        // v1.21.0 (I1, partial): --tool flag accepts cursor/windsurf/zed/codex/
        // copilot/opencode/gemini/amp. Currently emits a hint pointing at
        // each tool's per-project config location so the user can wire icmg
        // manually. Native auto-install per tool deferred to v1.21.x+.
        std::string tool = flagValue(args, "--tool", "claude-code");
        if (tool != "claude-code") {
            std::cout << "  --tool " << tool << ": auto-install not yet implemented (v1.21.x+).\n"
                      << "  Manual config locations:\n"
                      << "    cursor    .cursor/rules/\n"
                      << "    windsurf  .windsurfrules\n"
                      << "    zed       .zed/settings.json\n"
                      << "    codex     ~/.codex/hooks.json\n"
                      << "    copilot   .github/hooks/icm.json\n"
                      << "    opencode  ~/.config/opencode/plugins/icm.ts\n"
                      << "    gemini    ~/.gemini/settings.json\n"
                      << "    amp       (see Amp docs)\n"
                      << "  Falling through to default Claude Code setup.\n\n";
        }

        // Global strict flag: ~/.icmg/strict.flag â†’ enforce on every init/upgrade.
        if (!strict_read) {
            const char* home = std::getenv("HOME");
            if (!home) home = std::getenv("USERPROFILE");
            if (home) {
                fs::path flag = fs::path(home) / ".icmg" / "strict.flag";
                if (fs::exists(flag)) strict_read = true;
            }
        }

        fs::path root = fs::current_path();
        std::cout << "icmg init: " << root.string() << "\n";

        // T3: Cloud sync path detection â€” warn if .icmg/ would be synced.
        {
            std::string rstr = root.string();
            for (char& c : rstr) if (c == '\\') c = '/';
            std::string rl = rstr;
            for (char& c : rl) c = (char)std::tolower((unsigned char)c);
            static const char* cloud_markers[] = {
                "onedrive", "dropbox", "google drive", "googledrive",
                "icloud", "box sync", "boxsync", nullptr
            };
            for (auto** m = cloud_markers; *m; ++m) {
                if (rl.find(*m) != std::string::npos) {
                    std::cerr << "  [WARN] Project inside cloud-sync path (" << *m << ").\n"
                              << "         .icmg/data.db may corrupt under concurrent sync.\n"
                              << "         Move project out of sync folder or add .icmg to exclusions.\n";
                    break;
                }
            }
        }

        // T2: Set restrictive permissions on .icmg/ (owner-only, no group/world).
        {
            fs::path icmg_dir = root / ".icmg";
            if (fs::exists(icmg_dir)) {
                std::error_code ec;
#ifndef _WIN32
                fs::permissions(icmg_dir,
                    fs::perms::owner_all,
                    fs::perm_options::replace, ec);
                fs::path db = icmg_dir / "data.db";
                if (fs::exists(db))
                    fs::permissions(db,
                        fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, ec);
#else
                // v1.20.4: icacls WITHOUT /T (was recursive — slow on .icmg
                // containing many WAL/mirror sidecars on large projects).
                // Permissions on parent dir suffice; children inherit on create.
                // /C continues on error; 5s cap to prevent stall on locked files.
                std::string p = icmg_dir.string();
                std::string cmd = "icacls \"" + p + "\" /inheritance:r /grant:r \"%USERNAME%\":F /C /Q";
                core::safeExecShell(cmd, true, 5000);
#endif
            }
        }

        // v1.20.5: ensure `jq` is on PATH — the generated PostToolUse hooks
        // pipe tool input through `jq` to extract fields cheaply (no extra
        // icmg.exe cold-spawn). If jq is missing the hooks would silently
        // no-op. On Windows we attempt a one-time download of jq.exe into
        // the icmg.exe install directory; on POSIX we print apt/brew/dnf
        // hints. Failure is non-fatal.
        ensureJq();

        int steps = 0;
        if (!no_hooks)    { steps += installHooks(root, force, strict_read); }
        if (!no_agents)   { steps += installAgents(root, force); }
        if (!no_embedder) { steps += installEmbedder(force); }

        if (!no_scan) {
            std::cout << "  graph scan: run `icmg graph scan` to populate symbol index\n";
            std::cout << "  embed:      run `icmg embed memory` (uses bundled ONNX runtime; Python sidecar fallback only if ONNX missing)\n";
        }

        // v0.42.0 T-18: auto-start rule-daemon if not already running.
        // v1.19.0: ping timeout 3s → 1s (PING IPC trivial).
        {
            auto ping = core::safeExecShell("icmg rule-eval --tool PING 2>/dev/null", false, 1000);
            if (ping.exit_code != 0) {
                auto r = core::safeExecShell("icmg rule-daemon start 2>&1", false, 5000);
                if (r.exit_code == 0) {
                    std::cout << "  rule-daemon: started\n";
                } else {
                    std::cout << "  rule-daemon: run `icmg rule-daemon start` to enable enforcement\n";
                }
            } else {
                std::cout << "  rule-daemon: already running\n";
            }
        }

        // v0.44.0 + v1.18.1 + v1.19.0 + v1.20.4: auto-import CLAUDE.md / plan
        // files / skill index.
        // v1.20.4: detached background fan-out — no longer blocks init.
        //   Reports from users that init was hanging ~1h on real projects
        //   pointed to icmg parallel spawning 3 cold-start child icmg.exe
        //   processes, each hit by Defender real-time scan on shared binary
        //   in multi-user installs. Detaching ensures init returns immediately;
        //   imports complete async and write into the same project DB.
        // Logs written to .icmg/init-imports.log for post-mortem.
#ifdef _WIN32
        {
            // v1.78.4: use CreateProcessA(bInheritHandles=FALSE) so grandchild
            // processes do NOT inherit our stdout/stderr pipe handles. The old
            // safeExecShell("start /B ...") approach caused init to hang 30+
            // minutes: grandchildren that inherited the pipe write handle kept
            // ReadFile blocked in safeExecShell until they exited.
            std::string log_path = (root / ".icmg" / "init-imports.log").string();
            std::string cmd_str =
                "cmd.exe /C \"set ICMG_NO_AUTOSPAWN=1 && "
                "(icmg claudemd import --slim & icmg plan import & icmg skill index)"
                " > \"\"" + log_path + "\"\" 2>&1\"";
            std::vector<char> cmdBuf(cmd_str.begin(), cmd_str.end());
            cmdBuf.push_back('\0');
            STARTUPINFOA si{}; si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            // bInheritHandles=FALSE: grandchildren get no inherited pipe handles.
            // v2.0.14: CREATE_NO_WINDOW ALONE (was DETACHED_PROCESS|CREATE_NO_WINDOW).
            // Combining the two left cmd.exe with NO console, so each grandchild
            // (icmg skill index / claudemd / plan -- console apps) allocated a FRESH
            // console window => visible popup flash on `init --force`. With
            // CREATE_NO_WINDOW alone cmd.exe gets a HIDDEN console the children inherit.
            if (CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr,
                               FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            std::cout << "  imports:       claudemd / plan / skill running in background\n";
            std::cout << "                 (log: .icmg/init-imports.log)\n";
        }
#else
        {
            std::string log_path = (root / ".icmg" / "init-imports.log").string();
            std::string bg_cmd =
                "ICMG_NO_AUTOSPAWN=1 nohup sh -c '"
                "icmg claudemd import --slim; icmg plan import; icmg skill index"
                "' > \"" + log_path + "\" 2>&1 &";
            core::safeExecShell(bg_cmd, true, 3000);
            std::cout << "  imports:       claudemd / plan / skill running in background\n";
            std::cout << "                 (log: .icmg/init-imports.log)\n";
        }
#endif

        // v1.6.0: consolidate per-project schtasks into single icmg-service
        // iterator. cron_jobs table backs the tick loop in src/core/service_loop.cpp.
        // Sweep any legacy `icmg-{backup,maintain,mirror,sentinel,shadow-upgrade}-<hash>`
        // schtasks left over from earlier installs.
        {
            std::error_code _ec;
            std::string cwd = fs::current_path(_ec).string();
            int _swept = core::sweepLegacySchtasks();
            if (_swept > 0) {
                std::cout << "  cron:       swept " << _swept
                          << " legacy per-project schtasks\n";
            }
            core::CronStore cs(core::Config::instance().globalDbPath());
            if (!no_backup) {
                // v1.19.0: skip eager initial snapshot (was ≤30s).
                // First cron tick fires within 60m anyway; init now <1s here.
                cs.upsert(cwd, "backup snapshot --note auto-hourly", 60);
                std::cout << "  backup:     cron registered every 60m (initial snapshot deferred)\n";
            }
            if (!no_maintain) {
                std::cout << "  maintain:   registering 6h hygiene...\n";
                cs.upsert(cwd, "maintain run", 360);
                std::cout << "    OK: cron_jobs every 360m\n";
            }
            if (!no_mirror) {
                // v1.19.0: skip eager initial mirror sync (was ≤30s).
                // First cron tick fires within 15m anyway.
                cs.upsert(cwd, "mirror sync", 15);
                std::cout << "  mirror:     cron registered every 15m (initial sync deferred)\n";
            }
            if (!hasFlag(args, "--no-sentinel")) {
                std::cout << "  sentinel:   registering 15m watchdog...\n";
                cs.upsert(cwd, "sentinel run --quiet", 15);
                std::cout << "    OK: cron_jobs every 15m\n";
            }
            if (!hasFlag(args, "--no-auto-upgrade")) {
                std::cout << "  upgrade:    registering daily shadow check...\n";
                cs.upsert(cwd, "shadow-upgrade check", 1440);
                std::cout << "    OK: cron_jobs every 1440m\n";
            }
        }

        // v0.45.0: auto-add icmg.exe to Windows Defender exclusion list.
        // v1.20.4: skip if cache marker present — Add-MpPreference is slow
        // (~5-30s incl. PowerShell cold start) and the result is persistent
        // per-user. Cache marker at %USERPROFILE%\.icmg\defender-excluded.flag.
        // Non-fatal: skips silently if elevation unavailable.
#ifdef _WIN32
        {
            bool _defender_skip = false;
            const char* home = std::getenv("USERPROFILE");
            if (home && *home) {
                fs::path marker = fs::path(home) / ".icmg" / "defender-excluded.flag";
                if (fs::exists(marker)) {
                    std::cout << "  defender:   cached exclusion (skip; remove "
                              << marker.string() << " to re-apply)\n";
                    _defender_skip = true;
                }
            }
          if (!_defender_skip) {
            char self_path[1024];
            DWORD n = GetModuleFileNameA(nullptr, self_path, sizeof(self_path));
            if (n > 0 && n < sizeof(self_path)) {
                std::string exe = self_path;
                for (auto& c : exe) if (c == '\\') c = '/';
                std::string cl = "powershell.exe -NoProfile -NonInteractive -Command "
                                 "\"Add-MpPreference -ExclusionProcess '" + exe + "'\"";
                std::vector<char> cl_buf(cl.begin(), cl.end());
                cl_buf.push_back('\0');
                STARTUPINFOA si{}; si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};
                if (CreateProcessA(nullptr, cl_buf.data(), nullptr, nullptr,
                                   FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, 5000);
                    DWORD ec = 1;
                    GetExitCodeProcess(pi.hProcess, &ec);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    if (ec == 0) {
                        std::cout << "  defender:   icmg.exe excluded (faster hook spawns)\n";
                        // v1.20.4: cache success so next init skips PowerShell.
                        const char* home2 = std::getenv("USERPROFILE");
                        if (home2 && *home2) {
                            std::error_code _ec_dir;
                            fs::create_directories(fs::path(home2) / ".icmg", _ec_dir);
                            std::ofstream m((fs::path(home2) / ".icmg" / "defender-excluded.flag").string());
                            m << "ok\n";
                        }
                    } else {
                        std::cout << "  defender:   exclusion needs elevation"
                                  << " â€” run `icmg init` as Administrator once to reduce Defender overhead\n";
                    }
                }
            }
          } // !_defender_skip
        }
#endif

        // M7-A: auto-register this project in global.db (best-effort, never fails init).
        try {
            auto& cfg = core::Config::instance();
            auto& gdb = core::GlobalDb::instance();
            std::string cwd_path;
            {
                std::error_code _ec;
                auto p = fs::current_path(_ec);
                if (!_ec) cwd_path = p.string();
                for (auto& ch : cwd_path) if (ch == '\\') ch = '/';
            }
            std::string proj_name = fs::path(cwd_path).filename().string();
            if (!cwd_path.empty() && !proj_name.empty() && !gdb.projectExists(proj_name)) {
                core::Project proj;
                proj.name = proj_name;
                proj.path = cwd_path;
                proj.db_path = cwd_path + "/.icmg/data.db";
                proj.description = "auto-registered by icmg init";
                gdb.addProject(proj);
                std::cout << "  project:    registered '"
                          << proj.name << "' in global registry\n";
            }
        } catch (...) {} // never block init

        std::cout << "\nDone. " << steps << " file(s) written.\n"
                  << "Restart your AI agent to pick up new hooks.\n";
        return 0;
    }

private:
    // v1.20.5: ensure jq is on PATH. PostToolUse hooks use jq inline for
    // cheap JSON extraction. On Windows attempt download of the official
    // standalone jq.exe into icmg.exe's install dir (which is already on
    // PATH for users who invoke icmg from CMD). On POSIX print install
    // hints — auto-install across distros is out of scope.
    void ensureJq() {
        // Probe presence.
        auto probe = core::safeExecShell("jq --version 2>&1", true, 3000);
        if (probe.exit_code == 0) {
            std::cout << "  jq:         present (" << probe.out.substr(0, 12) << ")\n";
            return;
        }

#ifdef _WIN32
        // Locate icmg.exe install dir; download jq.exe sibling.
        char self_path[1024];
        DWORD n = GetModuleFileNameA(nullptr, self_path, sizeof(self_path));
        if (n == 0 || n >= sizeof(self_path)) {
            std::cout << "  jq:         not found (could not locate install dir)\n";
            return;
        }
        std::string sp = self_path;
        size_t last_sep = sp.find_last_of("\\/");
        if (last_sep == std::string::npos) {
            std::cout << "  jq:         not found (install dir unresolved)\n";
            return;
        }
        std::string install_dir = sp.substr(0, last_sep);
        fs::path jq_target = fs::path(install_dir) / "jq.exe";
        if (fs::exists(jq_target)) {
            std::cout << "  jq:         present in install dir\n";
            return;
        }

        std::cout << "  jq:         not found — downloading official jq-windows-amd64.exe...\n";
        // Official release URL (stable). 6.5MB; SHA verified by GitHub.
        const std::string url =
            "https://github.com/jqlang/jq/releases/download/jq-1.7.1/jq-windows-amd64.exe";
        std::string tmp_path = (fs::temp_directory_path() / "jq-icmg-download.exe").string();
        std::string curl_cmd =
            std::string(core::curlBin()) + " -fsSL --max-time 60 -o \"" + tmp_path + "\" \"" + url + "\"";
        auto dl = core::safeExecShell(curl_cmd, true, 65000);
        if (dl.exit_code != 0 || !fs::exists(tmp_path)) {
            std::cout << "  jq:         download failed — install manually from "
                      << "https://jqlang.org/download/ (place jq.exe in " << install_dir << ")\n";
            return;
        }
        std::error_code ec;
        fs::rename(tmp_path, jq_target, ec);
        if (ec) {
            fs::copy_file(tmp_path, jq_target,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::cout << "  jq:         download ok but install failed ("
                          << ec.message() << "); copy " << tmp_path
                          << " → " << jq_target.string() << " manually\n";
                return;
            }
        }
        std::cout << "  jq:         installed at " << jq_target.string() << "\n";
#else
        std::cout << "  jq:         not found — install with one of:\n"
                  << "                Ubuntu/Debian:  sudo apt install -y jq\n"
                  << "                Fedora/RHEL:    sudo dnf install -y jq\n"
                  << "                Arch:           sudo pacman -S jq\n"
                  << "                macOS:          brew install jq\n";
#endif
    }

    int installHooks(const fs::path& root, bool force, bool strict_read = false) {
        int n = 0;
        fs::create_directories(root / ".claude" / "hooks");

        // Drop hook scripts â€” always overwrite so upgrades fix stale/buggy hooks
        // (e.g. old versions with & background that caused WAL 65GB growth).
        // Users who need custom hooks should use --no-hooks and manage manually.
        n += writeFile(root / ".claude" / "hooks" / "icmg-bash-rewrite.sh", BASH_REWRITE_SH, true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-shrink-read.sh", SHRINK_READ_SH, true);
        // Phase 45 T3: cap-output PostToolUse hook (auto-shrink Bash >50KB).
        n += writeFile(root / ".claude" / "hooks" / "icmg-cap-output.sh", CAP_OUTPUT_SH, true);
        // v1.31.0 A0: PreCompact .py no longer written. settings.json
        // calls `icmg hook precompact` C++ handler directly (v0.54.0+).
        {
            fs::path stale_py = root / ".claude" / "hooks" / "icmg-precompact-snapshot.py";
            std::error_code _rmec;
            if (fs::exists(stale_py, _rmec)) {
                fs::remove(stale_py, _rmec);
                std::cout << "  cleanup: removed stale icmg-precompact-snapshot.py\n";
            }
        }
        // v1.78.1: caveman -> sayless migration. Auto-rename old flag files
        // + remove stale caveman hook script (idempotent; no-op if nothing to do).
        {
            int migrated_proj = migrateCavemanToSayless(fs::current_path());
            const char* h_mig = std::getenv("HOME");
            if (!h_mig) h_mig = std::getenv("USERPROFILE");
            int migrated_global = h_mig ? migrateCavemanToSayless(fs::path(h_mig)) : 0;
            if (migrated_proj + migrated_global > 0) {
                std::cout << "  cleanup: migrated caveman flag(s) -> sayless ("
                          << (migrated_proj + migrated_global) << " file(s))\n";
            }
            std::error_code _rmec;
            fs::path stale_hook = root / ".claude" / "hooks" / "icmg-caveman-prompt.sh";
            if (fs::exists(stale_hook)) {
                fs::remove(stale_hook, _rmec);
                std::cout << "  cleanup: removed stale icmg-caveman-prompt.sh (renamed to sayless)\n";
            }
        }
        n += writeFile(root / ".claude" / "hooks" / "icmg-sayless-prompt.sh", SAYLESS_PROMPT_SH, true);
        // Auto-enable sayless ultra on init if flag absent (never overwrite existing level).
        {
            const char* h2 = std::getenv("HOME");
            if (!h2) h2 = std::getenv("USERPROFILE");
            if (h2) {
                fs::path cflag = fs::path(h2) / ".icmg" / "sayless.flag";
                if (!fs::exists(cflag)) {
                    fs::create_directories(cflag.parent_path());
                    std::ofstream ofs(cflag);
                    ofs << "ultra\n";
                    std::cout << "  sayless:    ultra mode enabled (~/.icmg/sayless.flag)\n";
                }
            }
        }
        // Phase 71: UserPromptSubmit auto-recall + suggest compress.
        n += writeFile(root / ".claude" / "hooks" / "icmg-prompt-recall.sh", PROMPT_RECALL_SH, true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-persona-inject.sh", PERSONA_INJECT_SH, true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-icmg-first.sh", ICMG_FIRST_SH, true);  // v1.69.0
        // v1.55.0 G1: advisor proactive hint hook.
        n += writeFile(root / ".claude" / "hooks" / "icmg-advisor.sh", ADVISOR_SH, true);
        // v1.47.0: local-route hook (prefix-based casual chat to local LLM).
        n += writeFile(root / ".claude" / "hooks" / "icmg-local-route.sh", LOCAL_ROUTE_SH, true);
        // Stop hook: wflog reminder on session end when git has changes.
        n += writeFile(root / ".claude" / "hooks" / "icmg-wflog-stop.sh", WFLOG_STOP_SH, true);
        // v0.42.0: context graph injection hooks.
        n += writeFile(root / ".claude" / "hooks" / "icmg-context-session.sh", CONTEXT_SESSION_SH, true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-context-prompt.sh",  CONTEXT_PROMPT_SH,  true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-mcp-filter.sh", MCP_FILTER_SH, true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-edit-expand.sh", EDIT_DIFF_EXPAND_SH, true);
        // #1084: wake-up briefing on SessionStart.
        n += writeFile(root / ".claude" / "hooks" / "icmg-wakeup-session.sh", WAKEUP_SESSION_SH, true);
        // v1.73.0: post-compaction memory/rules re-anchor.
        n += writeFile(root / ".claude" / "hooks" / "icmg-postcompact-memory.sh", POSTCOMPACT_MEMORY_SH, true);
        // v0.42.0: rule enforcement hook.
        n += writeFile(root / ".claude" / "hooks" / "icmg-rule-enforce.sh", RULE_ENFORCE_SH, true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-presence-heartbeat.sh", PRESENCE_HEARTBEAT_SH, true);  // me-everywhere
        // v1.19.2: git-leash + graph-update shim. settings.local.json wires both.
        n += writeFile(root / ".claude" / "hooks" / "icmg-git-leash.sh",    GIT_LEASH_SH,    true);
        // v1.25.0 (W1): compressed-write rule injector — zero-cost when flag absent.
        n += writeFile(root / ".claude" / "hooks" / "icmg-compressed-write.sh",
                       COMPRESSED_WRITE_RULE_SH, true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-graph-update.sh", GRAPH_UPDATE_SH, true);

#ifndef _WIN32
        // chmod +x on POSIX
        for (auto* f : {"icmg-bash-rewrite.sh", "icmg-shrink-read.sh"}) {
            std::string p = (root / ".claude" / "hooks" / f).string();
            std::error_code ec;
            fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, ec);
        }
#endif

        // Merge into settings.local.json.
        fs::path sp = root / ".claude" / "settings.local.json";
        json cfg;
        if (fs::exists(sp)) {
            try { std::ifstream f(sp); f >> cfg; } catch (...) { cfg = json::object(); }
        } else {
            cfg = json::object();
        }
        if (!cfg.contains("hooks")) cfg["hooks"] = json::object();
        // v1.20.5: drop `icmg shield --` wrapper around hook scripts. shield
        // cold-spawned an extra icmg.exe per tool invocation, which on first
        // launch piled onto the 60s subprocess-init budget (Defender scan
        // every spawn). Hooks run as plain `bash <script>` now; the scripts
        // themselves invoke icmg lazily and exit fast on missing dependency.
        json pre_array = json::array({
            {
                // v1.26.0: matcher widened "Bash" → "Bash|PowerShell". AI was
                // bypassing icmg-first + bash-rewrite + RAW=1 nag by routing
                // commands through PowerShell tool, which previously matched
                // ZERO PreToolUse entries. The leash script already inspects
                // $TOOL var ("Bash"|"PowerShell") so no code change needed —
                // only matcher widen here. bash-rewrite checks pattern after
                // strip leading $env: prefix on PS6 commands.
                {"matcher", "Bash|PowerShell"},
                {"hooks",   json::array({
                    // v1.21.9: wrap every test+exec one-liner in `bash -c`. Some
                    // Claude Code hook runners exec the command directly (without
                    // an implicit `sh -c`), and on those `[ -f X ]` becomes a
                    // call to `/usr/bin/[` binary with the rest of the string as
                    // a single argv — yielding `cannot execute binary file`.
                    // The bash -c wrapper guarantees shell parsing on every host.
                    {{"type", "command"},
                     {"timeout", 10},
                     {"command",
                        std::string("bash -c '[ -f .claude/hooks/icmg-git-leash.sh ] && bash .claude/hooks/icmg-git-leash.sh || exit 0'")}},
                    {{"type", "command"},
                     {"timeout", 5},
                     {"command",
                        std::string("bash -c '[ -f .claude/hooks/icmg-bash-rewrite.sh ] && ") +
                        (strict_read ? "ICMG_STRICT_BASH=1 " : "") +
                        "bash .claude/hooks/icmg-bash-rewrite.sh || exit 0'"}}
                })}
            },
            {
                {"matcher", "Write"},
                {"hooks",   json::array({
                    {{"type", "command"},
                     {"timeout", 10},
                     {"command",
                        std::string("bash -c '[ -f .claude/hooks/icmg-git-leash.sh ] && bash .claude/hooks/icmg-git-leash.sh || exit 0'")}},
                    // v1.25.0 (W1): compressed-write rule injector. Zero-cost when
                    // ~/.icmg/write-mode.flag absent (default).
                    {{"type", "command"},
                     {"timeout", 3},
                     {"command",
                        std::string("bash -c '[ -f .claude/hooks/icmg-compressed-write.sh ] && bash .claude/hooks/icmg-compressed-write.sh || exit 0'")}}
                })}
            },
            {
                {"matcher", "Read|Glob|Grep"},
                {"hooks",   json::array({
                    {{"type", "command"},
                     {"timeout", 5},
                     {"command",
                        "bash -c '[ -f .claude/hooks/icmg-rule-enforce.sh ] && bash .claude/hooks/icmg-rule-enforce.sh || exit 0'"}}
                })}
            },
            {
                {"matcher", "Read"},
                {"hooks",   json::array({
                    {{"type", "command"},
                     {"timeout", 5},
                     {"command",
                        std::string("bash -c '[ -f .claude/hooks/icmg-shrink-read.sh ] && ") +
                        "ICMG_READ_LIMIT=30 ICMG_SHRINK_THRESHOLD=0 " +
                        (strict_read ? "ICMG_SHRINK_STRICT=1 " : "") +
                        "bash .claude/hooks/icmg-shrink-read.sh || exit 0'"}}
                })}
            },
            // v2.0.13: PreToolUse MCP browser-automation deny (audit mode).
            {
                {"matcher", "mcp__.*"},
                {"hooks",   json::array({
                    {{"type", "command"},
                     {"timeout", 4},
                     {"command",
                        "bash -c 'command -v icmg >/dev/null 2>&1 && icmg hook pretooluse || exit 0'"}}
                })}
            }
        });
        // Phase 56 T1: WebFetch redirect to `icmg fetch` (cache + reduce â†’ 70-90% off).
        // Strict mode hard-denies; soft mode emits suggestion but allows.
        if (strict_read) {
            pre_array.push_back({
                {"matcher", "WebFetch"},
                {"hooks", json::array({
                    {{"type", "command"}, {"command",
                        "INPUT=$(cat); URL=$(printf '%s' \"$INPUT\" | icmg hookio get tool_input.url 2>/dev/null); "
                        "[ -z \"$URL\" ] && exit 0; "
                        "HOMED=\"${USERPROFILE:-${HOME:-/tmp}}\"; mkdir -p \"$HOMED/.icmg\" 2>/dev/null; "
                        "printf '{\"ts\":%s,\"hook\":\"webfetch-strict\",\"target\":%s,\"reason\":\"WebFetch denied\"}\\n' \"$(date +%s)\" \"$(printf '%s' \"$URL\" | icmg hookio escape)\" >> \"$HOMED/.icmg/strict-denials.jsonl\" 2>/dev/null || true; "
                        "icmg hookio emit PreToolUse --deny \"STRICT mode: use `icmg fetch $URL` (cached + reduced, 70-90% token saving). Bypass: icmg strict off.\"; "
                        "exit 2"}}
                })}
            });
        }
        // (PreToolUse assigned later after Phase 70 Edit hook appended.)
        // Phase 45 T3: PostToolUse:Bash â€” auto-shrink huge outputs (>8KB).
        // Phase 67 T4: PostToolUse:Edit â€” capture user fixes to AI-emitted code.
        // Phase 70: PostToolUse:Glob/Grep/WebFetch + universal cap for any
        // tool result >8KB. Coverage extension targeting outside-icmg waste.
        cfg["hooks"]["PostToolUse"] = json::array({
            {
                {"matcher", "Bash|PowerShell"},
                {"hooks", json::array({
                    // v1.21.9: bash -c wrapper (see PreToolUse comment above).
                    {{"type", "command"},
                     {"command", "bash -c '[ -f .claude/hooks/icmg-cap-output.sh ] && (command -v icmg >/dev/null 2>&1 && icmg shield -- bash .claude/hooks/icmg-cap-output.sh) || bash .claude/hooks/icmg-cap-output.sh || exit 0'"}}
                })}
            },
            {
                {"matcher", "Edit"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "icmg correction capture 2>/dev/null || true"}}
                })}
            },
            // Phase 70: Glob/Grep cap â€” both produce path/line lists that
            // accumulate fast. Cap to top 50 entries.
            {
                {"matcher", "Glob|Grep"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command",
                      "INPUT=$(cat); "
                      "OUT=$(printf '%s' \"$INPUT\" | icmg hookio get tool_response.output 2>/dev/null); [ -z \"$OUT\" ] && OUT=$(printf '%s' \"$INPUT\" | icmg hookio get tool_response.content 2>/dev/null); "
                      "LINES=$(printf '%s' \"$OUT\" | wc -l); "
                      "[ \"$LINES\" -lt 50 ] && exit 0; "
                      "HEAD=$(printf '%s' \"$OUT\" | head -50); "
                      "MSG=$(printf '%s\\n... [%d total entries; first 50 shown â€” refine query for fewer] ...\\n' \"$HEAD\" \"$LINES\"); "
                      "printf '%s' \"$MSG\" | icmg hookio emit PostToolUse --ctx-stdin"}}
                })}
            },
            // Phase 70: WebFetch result cap â€” even after icmg fetch reduce,
            // direct WebFetch can return large pages. Cap to 4KB.
            {
                {"matcher", "WebFetch"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command",
                      "INPUT=$(cat); "
                      "OUT=$(printf '%s' \"$INPUT\" | icmg hookio get tool_response.content 2>/dev/null); [ -z \"$OUT\" ] && OUT=$(printf '%s' \"$INPUT\" | icmg hookio get tool_response.output 2>/dev/null); "
                      "SZ=${#OUT}; "
                      "[ \"$SZ\" -lt 4096 ] && exit 0; "
                      "HEAD=$(printf '%s' \"$OUT\" | head -c 4000); "
                      "MSG=$(printf '%s\\n... [WebFetch capped from %d to 4KB; use icmg fetch for cached + reduced] ...\\n' \"$HEAD\" \"$SZ\"); "
                      "printf '%s' \"$MSG\" | icmg hookio emit PostToolUse --ctx-stdin"}}
                })}
            },
            // Phase 82 T2: PostToolUse:Read â€” graph-aware auto-reroute.
            // If file in graph: emit icmg context (structured, ~80% cut).
            // Fallback: icmg shrink. Threshold 4KB (ICMG_READ_THRESHOLD env).
            {
                {"matcher", "Read"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command",
                      "INPUT=$(cat); "
                      "FILE=$(printf '%s' \"$INPUT\" | icmg hookio get tool_input.file_path 2>/dev/null); "
                      "OUT=$(printf '%s' \"$INPUT\" | icmg hookio get tool_response.content 2>/dev/null); [ -z \"$OUT\" ] && OUT=$(printf '%s' \"$INPUT\" | icmg hookio get tool_response.output 2>/dev/null); "
                      "SZ=${#OUT}; "
                      "THRESH=${ICMG_READ_THRESHOLD:-4096}; "
                      "[ \"$SZ\" -lt \"$THRESH\" ] && exit 0; "
                      "command -v icmg >/dev/null 2>&1 || exit 0; "
                      "if [ -n \"$FILE\" ] && [ -f \"$FILE\" ]; then "
                      "  CTX=$(icmg context \"$FILE\" --max-bytes 3000 2>/dev/null); "
                      "  if [ -n \"$CTX\" ]; then "
                      "    MSG=$(printf '[Read auto-rerouted to icmg context (%dB â†’ structured)]\\n%s\\nHint: use `icmg context %s` directly.' \"$SZ\" \"$CTX\" \"$FILE\"); "
                      "    printf '%s' \"$MSG\" | icmg hookio emit PostToolUse --ctx-stdin; "
                      "    exit 0; "
                      "  fi; "
                      "fi; "
                      "SHRUNK=$(printf '%s' \"$OUT\" | icmg shrink --threshold 0 2>/dev/null); "
                      "[ -z \"$SHRUNK\" ] && exit 0; "
                      "OZ=${#OUT}; SZ2=${#SHRUNK}; "
                      "[ \"$SZ2\" -ge \"$OZ\" ] && exit 0; "
                      "MSG=$(printf 'Read output shrunk (%dB â†’ %dB). Use `icmg context %s` for structured output.\\n%s' \"$OZ\" \"$SZ2\" \"$FILE\" \"$SHRUNK\"); "
                      "printf '%s' \"$MSG\" | icmg hookio emit PostToolUse --ctx-stdin"}}
                })}
            },
            // v1.4.0 Task 3 + v1.19.2: PostToolUse:Edit|Write auto graph-update + memory draft.
            // v1.19.2: route via icmg-graph-update.sh shim (settings.local.json
            // visibility) which delegates to in-process posttooluse-edit handler.
            {
                {"matcher", "Edit|Write"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"timeout", 30},
                     {"async", true},
                     // v1.22.1: bash -c wrap (same fix as v1.21.9 for other entries).
                     {"command",
                      "bash -c '[ -f .claude/hooks/icmg-graph-update.sh ] && bash .claude/hooks/icmg-graph-update.sh || (command -v icmg >/dev/null 2>&1 && echo \"$(cat)\" | icmg hook posttooluse-edit 2>/dev/null || true)'"}}
                })}
            }
        });
        cfg["hooks"]["PreToolUse"] = pre_array;

        // Phase 51 T2: SessionStart sayless directive.
        // v0.42.0: also inject hot context_nodes at session start.
        // v1.20.5: revert to script-based SessionStart entries (user-confirmed
        // working). Combined `icmg session-inject` had been triggering the
        // first-launch "Subprocess initialization did not complete within
        // 60000ms" cascade on cold icmg.exe spawn. Script invocations exit
        // fast when scripts are missing (`|| exit 0` tail) and the warm-up
        // daemon/scan lines that previously sat here are dropped — they were
        // contributing to the same cold-spawn budget hit on first launch.
        cfg["hooks"]["SessionStart"] = json::array({
            {
                // v1.22.1: bash -c wrap on all SessionStart entries.
                {"hooks", json::array({
                    {{"type", "command"},
                     {"timeout", 5},
                     {"command", "bash -c '[ -f .claude/hooks/icmg-sayless-prompt.sh ] && bash .claude/hooks/icmg-sayless-prompt.sh || exit 0'"}},
                    {{"type", "command"},
                     {"timeout", 5},
                     {"command", "bash -c '[ -f .claude/hooks/icmg-context-session.sh ] && bash .claude/hooks/icmg-context-session.sh || exit 0'"}},
                    {{"type", "command"},
                     {"timeout", 5},
                     {"command", "bash -c '[ -f .claude/hooks/icmg-wakeup-session.sh ] && bash .claude/hooks/icmg-wakeup-session.sh || exit 0'"}},
                    {{"type", "command"},
                     {"timeout", 12},
                     {"command", "bash -c '[ -f .claude/hooks/icmg-postcompact-memory.sh ] && bash .claude/hooks/icmg-postcompact-memory.sh || exit 0'"}}
                })}
            }
        });

        // v1.5.3: UserPromptSubmit goes pure icmg.exe — drops bash + jq + daemon
        // IPC dance (icmg-prompt-recall.sh) that was spawning Win32 children
        // outside icmg.exe SetErrorMode coverage, causing the recurring B:/ popup
        // when transitive lookups hit MSYS path-translated drives.
        // v1.20.5: revert to script-based UserPromptSubmit. `exec icmg hook
        // userprompt` from the previous generator cold-spawned icmg.exe on
        // every prompt — exhausted Claude Code's 60s init budget on first
        // launch with cold Defender scan. Script invocation lets the rule
        // be evaluated lazily inside the script (which also exits fast).
        cfg["hooks"]["UserPromptSubmit"] = json::array({
            {
                // v1.22.1: bash -c wrap on UserPromptSubmit entry.
                {"hooks", json::array({
                    {{"type", "command"},
                     {"timeout", 5},
                     {"command", "bash -c '[ -f .claude/hooks/icmg-prompt-recall.sh ] && bash .claude/hooks/icmg-prompt-recall.sh || exit 0'"}},
                     {{"type", "command"},
                      {"timeout", 5},
                      {"command", "command -v icmg >/dev/null 2>&1 || exit 0; M=$(icmg mode get 2>/dev/null); [ -z \"$M\" ] && exit 0; printf 'MODE: %s' \"$M\" | icmg hookio emit UserPromptSubmit --ctx-stdin"}},
                     {{"type", "command"},
                      {"timeout", 5},
                      {"command", "command -v icmg >/dev/null 2>&1 || exit 0; H=$(icmg suggest --hook 2>/dev/null); [ -z \"$H\" ] && exit 0; printf '%s' \"$H\" | icmg hookio emit UserPromptSubmit --ctx-stdin"}}
                })}
            },
            {
                // v1.43.0 Phase 1: persona injection (per-user system prompt prefix).
                {"hooks", json::array({
                    {{"type", "command"},
                     {"timeout", 3},
                     {"command", "bash -c '[ -f .claude/hooks/icmg-persona-inject.sh ] && bash .claude/hooks/icmg-persona-inject.sh || exit 0'"}},
                    // v1.55.0 G1: advisor proactive hint.
                    {{"type", "command"},
                     {"timeout", 3},
                     {"command", "bash -c '[ -f .claude/hooks/icmg-advisor.sh ] && bash .claude/hooks/icmg-advisor.sh || exit 0'"}},
                      {{"type", "command"},
                       {"timeout", 3},
                       {"command", "bash -c '[ -f .claude/hooks/icmg-icmg-first.sh ] && bash .claude/hooks/icmg-icmg-first.sh || exit 0'"}}
                })}
            },
            {
                // v1.47.0: local-route (prefix-based casual chat to local LLM).
                {"hooks", json::array({
                    {{"type", "command"},
                     {"timeout", 30},  // LLM cold-load can take seconds.
                     {"command", "bash -c '[ -f .claude/hooks/icmg-local-route.sh ] && bash .claude/hooks/icmg-local-route.sh || exit 0'"}}
                })}
            },
            {
                // me-everywhere: heartbeat this session's presence each prompt so
                // parallel sessions on this machine see each other (live wire).
                {"hooks", json::array({
                    {{"type", "command"},
                     {"timeout", 5},
                     {"command", presenceHeartbeatHookCmd()}}
                })}
            }
        });

        // v0.54.0: PreCompact hook consolidated into `icmg hook precompact`.
        // Single fork invokes snapshot (Python) + distill session + ABSOLUTE RULE
        // re-injection. Saves outer bash + jq chain (~200-300ms).
        cfg["hooks"]["PreCompact"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"timeout", 10},  // v1.20.5: bounded; precompact may include snapshot.
                     {"command", "command -v icmg >/dev/null 2>&1 || exit 0; icmg hook precompact 2>/dev/null || exit 0"}}
                })}
            }
        });
        // v1.20.5: revert PreToolUse to per-matcher script-based entries
        // (user-confirmed working). The previous `exec icmg hook pretooluse`
        // single-entry pattern cold-spawned icmg.exe on EVERY tool call,
        // contributing to first-launch 60s init timeout. Per-matcher scripts
        // exit fast when script missing (`[ -f ... ] || exit 0`).
        // pre_array (built ~120 lines above) already covers Bash + Write +
        // Read|Glob|Grep + Read + (strict_read ? WebFetch) — keep that
        // canonical structure rather than overwriting with the hard-deny block.

        // v1.5.3: Stop hook trimmed to single icmg.exe call. Dropped the
        // wflog-stop.sh sidecar — it spawned git + jq (Win32) outside icmg's
        // SetErrorMode, triggering the recurring B:/ popup at end of turn.
        // wflog reminder feature retired; users invoke `icmg wflog save` manually.
        cfg["hooks"]["Stop"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "command -v icmg >/dev/null 2>&1 || exit 0; exec icmg hook stop"}},
                    {{"type", "command"},
                     {"timeout", 10},
                     {"command", "command -v icmg >/dev/null 2>&1 || exit 0; [ -n \"$ICMG_NO_COMPACT_ADVISE\" ] && exit 0; FILL=$(icmg context-budget --percent 2>/dev/null | tr -dc '0-9' | head -c 3); [ -z \"$FILL\" ] && exit 0; MSG=$(icmg govern advise --fill \"$FILL\" 2>/dev/null); [ -z \"$MSG\" ] && exit 0; printf '%s' \"$MSG\" | icmg hookio emit Stop --ctx-stdin"}},
                    {{"type", "command"},
                     {"timeout", 10},
                     {"command", "command -v icmg >/dev/null 2>&1 || exit 0; icmg prompt-capture 2>/dev/null || exit 0"}}
                })}
            }
        });
                // v2.0.x: ensure every command hook is timeout-bounded so a stalled hook
        // cannot trip an IDE's ~60s IPC connection limit (survives re-init; long-session
        // finding -- init --force previously reverted local per-hook timeouts).
        for (auto& evt : cfg["hooks"]) {
            for (auto& matcher : evt) {
                if (!matcher.contains("hooks")) continue;
                for (auto& h : matcher["hooks"]) {
                    if (h.value("type", std::string()) == "command" && !h.contains("timeout"))
                        h["timeout"] = 10;
                }
            }
        }
        std::ofstream f(sp);
        f << cfg.dump(2) << "\n";
        std::cout << "  + .claude/settings.local.json (hooks installed)\n";
        ++n;

        // Install icmg-first enforcement into global ~/.claude/settings.json
        // so the rule applies across ALL projects, not just this one.
        n += installGlobalReadHook(force);

        // v1.23.0: sanitize project-level `.claude/settings.json` if present.
        // Older icmg builds (pre-v1.20) wrote a SessionStart hook that shelled
        // out to `python3 -c "...icmg wake-up..."`. On hosts without python3
        // the hook prints `python3: command not found` to Claude's stderr
        // every time Write is invoked (Claude triggers SessionStart fan-out
        // on tool events too). Replace with pure-bash equivalent via
        // `icmg hookio emit SessionStart --ctx-stdin`.
        {
            fs::path proj_settings = root / ".claude" / "settings.json";
            if (fs::exists(proj_settings)) {
                try {
                    std::ifstream f(proj_settings);
                    std::string body((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
                    f.close();
                    const std::string py_marker = "python3 -c";
                    const std::string icmg_marker = "icmg','wake-up";
                    // Only rewrite when BOTH markers present — narrowest target.
                    if (body.find(py_marker) != std::string::npos
                        && body.find(icmg_marker) != std::string::npos) {
                        json proj_cfg;
                        try { proj_cfg = json::parse(body); }
                        catch (...) { proj_cfg = json::object(); }
                        if (proj_cfg.contains("hooks")
                            && proj_cfg["hooks"].is_object()
                            && proj_cfg["hooks"].contains("SessionStart")
                            && proj_cfg["hooks"]["SessionStart"].is_array()) {
                            for (auto& entry : proj_cfg["hooks"]["SessionStart"]) {
                                if (!entry.contains("hooks") || !entry["hooks"].is_array())
                                    continue;
                                for (auto& h : entry["hooks"]) {
                                    if (!h.contains("command") || !h["command"].is_string())
                                        continue;
                                    std::string cmd = h["command"].get<std::string>();
                                    if (cmd.find(py_marker) != std::string::npos
                                        && cmd.find("wake-up") != std::string::npos) {
                                        h["command"] =
                                            "bash -c 'command -v icmg >/dev/null 2>&1 || exit 0; "
                                            "icmg wake-up 2>/dev/null | icmg hookio emit SessionStart "
                                            "--ctx-stdin'";
                                    }
                                }
                            }
                            std::ofstream out(proj_settings);
                            out << proj_cfg.dump(2) << "\n";
                            std::cout << "  + .claude/settings.json (sanitized python3 -> bash)\n";
                            ++n;
                        }
                    }
                } catch (...) { /* best-effort */ }
            }
        }

        // v1.1.1: register resident service (Windows logon-trigger) + clean any
        // legacy per-project autopilot schtasks left over from pre-v1.1.0.
        // POSIX: helpers are no-op stubs. Opt-out via ICMG_SKIP_SERVICE=1.
#ifdef _WIN32
        if (!std::getenv("ICMG_SKIP_SERVICE")) {
            std::string serr;
            if (core::installResidentService(&serr)) {
                std::cout << "  + icmg service: logon-trigger installed\n";
            } else if (!serr.empty()) {
                std::cerr << "  ! icmg service install skipped: " << serr << "\n";
            }
            int removed = core::cleanupLegacySchtasks();
            if (removed > 0)
                std::cout << "  + icmg service: cleaned " << removed << " legacy schtasks\n";
        }
#endif

        // T11: auto-sync .icmgrules/ into rules_bank if directory exists.
        {
            fs::path rules_dir = root / ".icmgrules";
            if (fs::exists(rules_dir) && fs::is_directory(rules_dir)) {
                auto rules_cmd = core::Registry<cli::BaseCommand>::instance().create("rules");
                if (rules_cmd) {
                    int rc = rules_cmd->run({"sync"});
                    if (rc == 0)
                        std::cout << "  + icmg rules: .icmgrules/ synced\n";
                }
            }
        }

        // Persona-continuity: seed identity-agnostic continuity zones (idempotent -> user content safe).
        {
            core::Db& pdb = core::personaDbAvailable() ? core::personaDb()
                                                       : core::GlobalDb::instance().db();
            core::ProfileStore pps(pdb);
            int seeded = core::scaffoldPersona(pps, core::currentUser(), /*force=*/false);
            if (seeded > 0)
                std::cout << "  + persona: " << seeded << " continuity zones seeded\n";
        }
        return n;
    }

    // Writes PreToolUse Read|Glob|Grep icmg-first hook to ~/.claude/settings.json.
    // Idempotent: skips if already present (unless force).
    int installGlobalReadHook(bool force) {
        const char* home = std::getenv("HOME");
        if (!home) home = std::getenv("USERPROFILE");
        if (!home) return 0;

        fs::path global_settings = fs::path(home) / ".claude" / "settings.json";
        if (!fs::exists(global_settings)) return 0;

        json gcfg;
        try {
            std::ifstream f(global_settings);
            f >> gcfg;
        } catch (...) {
            gcfg = json::object();
        }

        if (!gcfg.contains("hooks")) gcfg["hooks"] = json::object();
        if (!gcfg["hooks"].is_object()) gcfg["hooks"] = json::object();

        const std::string matcher = "Read|Glob|Grep";
        // v1.22.1: pure-bash hook (no python3 dependency). Earlier versions
        // shelled out to `python3 -c "..."` which broke on hosts without
        // python (Alpine, minimal Docker, fresh Win without Python install).
        // printf with a here-string literal so the JSON survives shell
        // escaping unchanged. `cat >/dev/null` drains stdin so Claude Code
        // doesn't see EBADF.
        const std::string hook_cmd =
            "bash -c 'cat >/dev/null; "
            "printf %s "
            "'\\''{\"hookSpecificOutput\":{\"hookEventName\":\"PreToolUse\","
            "\"additionalContext\":\"ICMG-FIRST RULE: Before Read/Glob/Grep, "
            "use icmg first: icmg context <file>, icmg pack <task>, "
            "icmg graph symbol <Name>, icmg recall <query>, "
            "icmg graph search <query>. Direct tools only if icmg cannot cover it.\"}}'\\'''";

        json& pre = gcfg["hooks"]["PreToolUse"];
        if (!pre.is_array()) pre = json::array();

        // Check for existing entry.
        for (auto& entry : pre) {
            if (entry.contains("matcher") && entry["matcher"] == matcher) {
                if (!force) {
                    std::cout << "  = ~/.claude/settings.json icmg-first hook (exists; --force to overwrite)\n";
                    return 0;
                }
                entry["hooks"] = json::array({{{"type", "command"}, {"command", hook_cmd}, {"shell", "bash"}}});
                std::ofstream out(global_settings);
                out << gcfg.dump(2) << "\n";
                std::cout << "  + ~/.claude/settings.json (icmg-first Read|Glob|Grep hook updated)\n";
                return 1;
            }
        }

        pre.push_back({
            {"matcher", matcher},
            {"hooks", json::array({
                {{"type", "command"}, {"command", hook_cmd}, {"shell", "bash"}}
            })}
        });

        std::ofstream out(global_settings);
        out << gcfg.dump(2) << "\n";
        std::cout << "  + ~/.claude/settings.json (icmg-first Read|Glob|Grep hook installed)\n";
        return 1;
    }

    // Inject or replace a marker-delimited block in AGENTS.md.
    // Returns 1 if file was written, 0 if nothing changed.
    int injectBlock(const fs::path& ap,
                    const std::string& marker_start,
                    const std::string& marker_end,
                    const char* block_content,
                    const std::string& label_update,
                    const std::string& label_append) {
        std::string existing;
        if (fs::exists(ap)) {
            std::ifstream f(ap); std::ostringstream s; s << f.rdbuf(); existing = s.str();
        }
        if (existing.find(marker_start) != std::string::npos) {
            auto a = existing.find(marker_start);
            auto b = existing.find(marker_end);
            if (b == std::string::npos || b < a) return 0;
            std::string before = existing.substr(0, a);
            std::string after  = existing.substr(b + marker_end.size());
            std::ofstream f(ap);
            f << before << block_content << after;
            std::cout << "  + AGENTS.md (" << label_update << ")\n";
            return 1;
        }
        std::ofstream f(ap, std::ios::app);
        if (!existing.empty() && existing.back() != '\n') f << "\n";
        f << "\n" << block_content;
        std::cout << "  + AGENTS.md (" << label_append << ")\n";
        return 1;
    }

    int installAgents(const fs::path& root, bool force) {
        fs::path ap = root / "AGENTS.md";
        int n = 0;
        n += injectBlock(ap,
                         "<!-- icmg:start -->", "<!-- icmg:end -->",
                         AGENTS_BLOCK,
                         "icmg block updated", "icmg block appended");
        n += injectBlock(ap,
                         "<!-- icmg:commands:start -->", "<!-- icmg:commands:end -->",
                         COMMANDS_BLOCK,
                         "commands block updated", "commands block appended");
        return n > 0 ? 1 : 0;
    }

    int writeFile(const fs::path& p, const std::string& content, bool force) {
        if (fs::exists(p) && !force) {
            std::cout << "  = " << p.filename().string() << " (exists; --force to overwrite)\n";
            return 0;
        }
        std::ofstream f(p);
        f << content;
        std::cout << "  + " << fs::relative(p, fs::current_path()).string() << "\n";
        return 1;
    }

    int installEmbedder(bool force) {
        // Drop sidecar to ~/.icmg/embed/icmg_embedder.py â€” findScript() picks
        // it up there. Binary-only installs (no source tree) need this.
        const char* h = std::getenv("USERPROFILE");
        if (!h) h = std::getenv("HOME");
        if (!h) h = ".";
        fs::path dir = fs::path(h) / ".icmg" / "embed";
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path target = dir / "icmg_embedder.py";
        if (fs::exists(target) && !force) {
            std::cout << "  = " << target.string() << " (exists; --force to overwrite)\n";
            return 0;
        }
        std::ofstream f(target);
        if (!f) {
            std::cout << "  ! cannot write " << target.string() << "\n";
            return 0;
        }
        f << EMBEDDER_PY;
        std::cout << "  + " << target.string() << "\n";
#ifndef _WIN32
        fs::permissions(target,
            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
            fs::perm_options::add, ec);
#endif
        return 1;
    }
};

ICMG_REGISTER_COMMAND("init", InitCommand);

} // namespace icmg::cli
