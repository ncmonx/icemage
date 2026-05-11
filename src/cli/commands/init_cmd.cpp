// `icmg init` — bootstrap a project for icmg-aware AI agents.
//
// Creates / updates:
//   .claude/settings.local.json  — installs Bash-rewrite + Read-shrink hooks
//   .claude/hooks/               — drops hook scripts (bundled in binary)
//   AGENTS.md                    — universal routing rules (appends marker block)
//   .icmg/data.db                — auto-init via existing migration
//
// After init: AI agents (Claude Code / Cursor / etc.) auto-route raw bash
// commands through `icmg run`, get token-filtered output, and follow AGENTS.md
// guidance to prefer icmg's recall/context/pack instead of raw Read/Grep.
//
// Idempotent: re-running updates hooks + agents block; preserves user-added
// keys in settings.local.json.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

// Bundled hook scripts (must stay in sync with examples/hooks/*.sh).
static const char* BASH_REWRITE_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Edit at your own risk.
# PreToolUse:Bash — redirect noisy commands through `icmg run` for filtered output.
set -uo pipefail
INPUT=$(cat)
CMD=$(echo "$INPUT" | jq -r '.tool_input.command // empty' 2>/dev/null)
[[ -z "$CMD" ]] && exit 0
echo "$CMD" | grep -qE '^RAW=1 ' && exit 0
echo "$CMD" | grep -qE '(^|[ |&;])(icmg|rtk)[ ]+' && exit 0

# Phase 58: log strict denial as JSONL (~/.icmg/strict-denials.jsonl).
log_denial() {
    local hook="$1" target="$2" reason="$3"
    local home_dir="${USERPROFILE:-${HOME:-/tmp}}"
    mkdir -p "$home_dir/.icmg" 2>/dev/null || return 0
    printf '{"ts":%s,"hook":"%s","target":%s,"reason":%s}\n' \
        "$(date +%s)" "$hook" \
        "$(printf '%s' "$target" | jq -Rs .)" \
        "$(printf '%s' "$reason" | jq -Rs .)" \
        >> "$home_dir/.icmg/strict-denials.jsonl" 2>/dev/null || true
}

# Strict mode: cat/head/tail/less/more on a file >20KB → hard-deny, force icmg context.
if [[ "${ICMG_STRICT_BASH:-0}" = "1" ]]; then
    FILE_CMD=$(echo "$CMD" | grep -oE '^[[:space:]]*(cat|head|tail|less|more)[[:space:]]+[^ |&;<>]+' | awk '{print $2}')
    if [[ -n "$FILE_CMD" && -f "$FILE_CMD" ]]; then
        SIZE=$(stat -c%s "$FILE_CMD" 2>/dev/null || stat -f%z "$FILE_CMD" 2>/dev/null || echo 0)
        if [[ "$SIZE" -gt 20000 ]]; then
            log_denial "bash-strict" "$FILE_CMD" "cat/head/tail on file ${SIZE}B"
            jq -n --arg f "$FILE_CMD" --arg sz "$SIZE" '{
                hookSpecificOutput: {
                    hookEventName: "PreToolUse",
                    permissionDecision: "deny",
                    permissionDecisionReason: ("File " + $f + " is " + $sz + " bytes. STRICT mode: use `icmg context " + $f + "` instead of cat/head/tail. Bypass: ICMG_STRICT_BASH=0.")
                }
            }'
            exit 2
        fi
    fi
fi

PATTERN='^[[:space:]]*(grep|rg|ag|fd|find|ls|cat|head|tail|wc|awk|sed|tree|du|node|deno|bun|ts-node|tsx|python|python3|py|ruby|php|java|perl|lua|cargo build|cargo test|cargo check|npm test|npm run build|yarn build|jest|vitest|pytest|dotnet build|dotnet test|dotnet run|go build|go test|go run|cmake|make|ninja|msbuild|gradle build|mvn|sqlcmd|osql|mysql|mariadb|psql|git log|git diff|git show|git status)([[:space:]]|$)'
if echo "$CMD" | grep -qE "$PATTERN"; then
    log_denial "bash-rewrite" "$CMD" "noisy command — use icmg run"
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
)BASH";

static const char* SHRINK_READ_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. PreToolUse:Read — inject `icmg summarize` for large files.
set -uo pipefail
INPUT=$(cat)
FILE=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty' 2>/dev/null)
[[ -z "$FILE" ]] && exit 0
[[ ! -f "$FILE" ]] && exit 0

# Phase 70: repeat-Read dedup — same file Read twice in same session at full
# range = waste. Track Read history at ~/.icmg/read-log-<session>.txt; if
# file already Read in last 30min and current Read has no offset/limit,
# force tiny limit (10 lines) since model already has full content.
HOMED="${USERPROFILE:-${HOME:-/tmp}}"
mkdir -p "$HOMED/.icmg" 2>/dev/null || true
LOG="$HOMED/.icmg/read-log.txt"
NOW=$(date +%s)
OFFSET=$(echo "$INPUT" | jq -r '.tool_input.offset // empty' 2>/dev/null)
LIMIT_IN=$(echo "$INPUT" | jq -r '.tool_input.limit // empty' 2>/dev/null)
if [[ -z "$OFFSET" && -z "$LIMIT_IN" && -f "$LOG" ]]; then
    # Find prior Read of same file within 1800s window.
    while IFS=$'\t' read -r ts path; do
        [[ "$path" != "$FILE" ]] && continue
        age=$((NOW - ts))
        [[ "$age" -gt 1800 ]] && continue
        # Already Read recently at full range → cap re-Read aggressively.
        jq -n --arg f "$FILE" --argjson lim 10 '{
            hookSpecificOutput: {
                hookEventName: "PreToolUse",
                permissionDecision: "allow",
                updatedInput: {file_path: $f, limit: $lim},
                additionalContext: ("REPEAT-READ dedup: " + $f + " was Read in last 30min. Capped to 10 lines. Use offset+limit for specific re-read.")
            }
        }'
        exit 0
    done < "$LOG"
fi
# Append to log (best-effort, capped at 200 lines via tail).
printf '%s\t%s\n' "$NOW" "$FILE" >> "$LOG" 2>/dev/null || true
# Trim log if huge.
[[ -f "$LOG" ]] && [[ $(wc -l < "$LOG") -gt 200 ]] && tail -100 "$LOG" > "$LOG.tmp" && mv "$LOG.tmp" "$LOG"

THRESHOLD="${ICMG_SHRINK_THRESHOLD:-60000}"
INC="${ICMG_SHRINK_INCLUDE:-}"
EXC="${ICMG_SHRINK_EXCLUDE:-}"
SIZE=$(stat -c%s "$FILE" 2>/dev/null || stat -f%z "$FILE" 2>/dev/null || echo 0)
# Phase 76: ICMG_READ_FORCE=1 always-route Read → icmg context overlay,
# regardless of file size or include/exclude pattern. Default-ON since
# v0.34.1. Opt-out via ICMG_READ_FORCE=0 (or ICMG_NO_READ_FORCE=1).
FORCE="${ICMG_READ_FORCE:-1}"
[[ "${ICMG_NO_READ_FORCE:-0}" = "1" ]] && FORCE=0
if [[ "$FORCE" != "1" ]]; then
    [[ -n "$INC" ]] && ! echo "$FILE" | grep -qE "$INC" && exit 0
    [[ -n "$EXC" ]] && echo "$FILE" | grep -qE "$EXC" && exit 0
    [[ "$SIZE" -lt "$THRESHOLD" ]] && exit 0
fi
SUMMARY=$(icmg summarize "$FILE" 2>/dev/null || true)
# Phase 76: when FORCE=1, missing summary is OK — icmg context still useful.
# Only abort when both summary AND context empty (file totally unknown).
CTX=$(icmg context "$FILE" --max-bytes 2048 --no-content 2>/dev/null || true)
[[ -z "$SUMMARY" && -z "$CTX" && "$FORCE" != "1" ]] && exit 0
[[ -z "$SUMMARY" ]] && SUMMARY="(no symbols indexed yet — run \`icmg graph update\`)"

# Phase 70: STRICT mode also uses updatedInput.limit cap (was hard-deny).
# Hard-deny broke Edit's "Read-first" session requirement. Cap to 50 lines
# in strict mode (vs 100 default) — still enforces "use icmg" while letting
# downstream Edit succeed. Logs denial-style entry for visibility.
if [[ "${ICMG_SHRINK_STRICT:-0}" = "1" ]]; then
    HOMED="${USERPROFILE:-${HOME:-/tmp}}"
    mkdir -p "$HOMED/.icmg" 2>/dev/null || true
    printf '{"ts":%s,"hook":"%s","target":%s,"reason":%s}\n' \
        "$(date +%s)" "read-strict" \
        "$(printf '%s' "$FILE" | jq -Rs .)" \
        "$(printf '%s' "Read capped on file ${SIZE}B" | jq -Rs .)" \
        >> "$HOMED/.icmg/strict-denials.jsonl" 2>/dev/null || true
    STRICT_LIMIT="${ICMG_READ_STRICT_LIMIT:-30}"
    jq -n --arg f "$FILE" --arg sz "$SIZE" --arg s "$SUMMARY" --arg c "$CTX" --argjson lim "$STRICT_LIMIT" '{
        hookSpecificOutput: {
            hookEventName: "PreToolUse",
            permissionDecision: "allow",
            updatedInput: {file_path: $f, limit: $lim},
            additionalContext: ("STRICT: file " + $f + " is " + $sz + " bytes. Read CAPPED to " + ($lim|tostring) + " lines (Edit-anchor only). For full slice use `icmg context " + $f + " --lines A-B`. For one symbol body use `icmg graph symbol <Name>`.\n\nicmg context (graph + symbols + memory):\n" + $c + "\n\nicmg summarize:\n" + $s)
        }
    }'
    exit 0
fi

# Phase 70: cap Read via updatedInput.limit instead of deny — satisfies Edit's
# "must Read first" session requirement while still saving tokens. User-side
# Edit tool checks for prior Read of same path; full deny breaks that flow.
# Default cap: 100 lines (~3KB ≈ 750 tok). User can re-Read with explicit
# offset+limit if more needed. Aggressive enough to save 80-90% on big files.
LIMIT="${ICMG_READ_LIMIT:-30}"
jq -n --arg s "$SUMMARY" --arg c "$CTX" --arg f "$FILE" --arg sz "$SIZE" --argjson lim "$LIMIT" '{
    hookSpecificOutput: {
        hookEventName: "PreToolUse",
        permissionDecision: "allow",
        updatedInput: {file_path: $f, limit: $lim},
        additionalContext: ("Large file " + $f + " (" + $sz + " bytes). Read CAPPED to " + ($lim|tostring) + " lines (Edit-anchor only). For full slice: `icmg context " + $f + " --lines A-B`. For symbol body: `icmg graph symbol <Name>`.\n\nicmg context (graph + symbols + memory):\n" + $c + "\n\nicmg summarize:\n" + $s)
    }
}'
exit 0
)BASH";

// Phase 51 T2: SessionStart hook injects caveman directive when flag present.
static const char* CAVEMAN_PROMPT_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Toggle via `icmg caveman on/off`.
set -uo pipefail
flag="${HOME:-$USERPROFILE}/.icmg/caveman.flag"
[[ -f "$flag" ]] || exit 0
level=$(head -n1 "$flag" 2>/dev/null || echo ultra)
date -u "+%Y-%m-%dT%H:%M:%SZ" > "${HOME:-$USERPROFILE}/.icmg/caveman-last-trigger.txt" 2>/dev/null || true
msg=$(printf '%s\n' "CAVEMAN MODE ACTIVE - level: ${level}." \
    "Respond terse. All technical substance stay. Only fluff die." \
    "Drop articles, filler, pleasantries, hedging. Fragments OK." \
    "Short synonyms. Technical terms exact. Code blocks unchanged." \
    "Pattern: [thing] [action] [reason]. [next step]." \
    "Code/commits/security/PRs: write normal." \
    "" \
    "THINKING PHASE rules (this is where verbose drift happens):" \
    "- Apply caveman ultra to internal thinking section too." \
    "- Internal reasoning: bullet fragments, no prose paragraphs." \
    "- Cap thinking to 80 words. If approach is obvious, skip thinking entirely." \
    "- No 'Let me check / Now I will / Looking at...' narration." \
    "- Decision form: '[option] -> [outcome]. pick [winner].'" \
    "- Repeating the question back inside thinking is forbidden." \
    "" \
    "Off only when user says 'stop caveman' or 'normal mode'.")
# Phase 67 T32: prepend violation pressure if recent caveman thinking-phase
# violations recorded. Escalates language at 2+ / 5+ violations in 24h.
# Phase 70: also surface real session token total to model — encourages
# self-throttling when token usage high.
if command -v icmg >/dev/null 2>&1; then
    pressure=$(icmg compliance inject 2>/dev/null)
    tok_summary=$(icmg context-budget --json --top 0 2>/dev/null | jq -r '.total_tokens // empty' 2>/dev/null)
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
jq -n --arg m "$msg" '{
    hookSpecificOutput: {
        hookEventName: "SessionStart",
        additionalContext: $m
    }
}'
)BASH";

// Phase 45 T3: PostToolUse:Bash hook routes huge stdout through `icmg shrink`.
static const char* CAP_OUTPUT_SH = R"BASH(#!/usr/bin/env bash
set -uo pipefail
out=$(jq -r '.tool_response.stdout // .tool_response.output // empty')
sz=${#out}
CAP=${ICMG_CAP_BYTES:-8000}
[[ "$sz" -le "$CAP" ]] && exit 0
if command -v icmg >/dev/null 2>&1; then
    shrunk=$(printf '%s' "$out" | icmg shrink --threshold 0 2>/dev/null)
    if [[ -n "$shrunk" ]]; then
        jq -n --arg m "$shrunk" '{hookSpecificOutput:{hookEventName:"PostToolUse",additionalContext:$m}}'
        exit 0
    fi
fi
hash=$(echo -n "$out" | sha1sum | cut -c1-8)
spill="/tmp/icmg-spill-$hash.txt"
printf '%s' "$out" > "$spill"
head_part=$(printf '%s' "$out" | head -c 4096)
tail_part=$(printf '%s' "$out" | tail -c 2048)
msg=$(printf '%s\n... [truncated, %d bytes total, full at %s] ...\n%s' "$head_part" "$sz" "$spill" "$tail_part")
jq -n --arg m "$msg" '{hookSpecificOutput:{hookEventName:"PostToolUse",additionalContext:$m}}'
)BASH";

// Phase 71: UserPromptSubmit hook — auto-recall memory + suggest compress
// when prompt contains large pasted text. Forces 4 core features (memory/
// compress/store/graph) into Claude's awareness on every turn instead of
// passive existence in DB.
static const char* PROMPT_RECALL_SH = R"BASH(#!/usr/bin/env bash
set -uo pipefail
INPUT=$(cat)
PROMPT=$(echo "$INPUT" | jq -r '.prompt // .message // empty' 2>/dev/null)
[ -z "$PROMPT" ] && exit 0
[ "${#PROMPT}" -lt 20 ] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0

# Recall memory hits matching prompt keywords (top 3).
# Phase 72 T9: cross-project fallback — when local hits sparse (<2 lines),
# try --all-projects to surface "this prompt was solved in project X" cases.
HITS=$(icmg recall "$PROMPT" --limit 3 2>/dev/null | head -30)
LOCAL_LINES=$(printf '%s' "$HITS" | wc -l)
if [ "$LOCAL_LINES" -lt 2 ] && [ "${ICMG_CROSS_PROJECT:-1}" = "1" ]; then
    XHITS=$(icmg recall "$PROMPT" --all-projects --limit 3 2>/dev/null | head -20)
    if [ -n "$XHITS" ]; then
        HITS="${HITS}"$'\n--- cross-project hits (other projects) ---\n'"${XHITS}"
    fi
fi
# Suggest compress when prompt large.
SZ=${#PROMPT}
SUGGEST=""
if [ "$SZ" -gt 4000 ]; then
    SUGGEST="(Large prompt ${SZ}B — model: pipe big paste through icmg compress before pasting next time.)"
fi
# Detect path mentions → pre-fetch icmg context.
FIRSTPATH=$(echo "$PROMPT" | grep -oE '[A-Za-z0-9_./\\-]+\.(cs|ts|tsx|js|jsx|py|cpp|hpp|sql|md|json|yaml|yml)' | head -1)
CTX=""
if [ -n "$FIRSTPATH" ] && [ -f "$FIRSTPATH" ]; then
    CTX=$(icmg context "$FIRSTPATH" --max-bytes 1024 --no-content 2>/dev/null || true)
fi

# Phase 75: drift check — flag prompts that touch pinned decision anchors.
# Phase 76: opt-out via ICMG_NO_DRIFT_CHECK=1 (skips ~50-200ms DB lookup).
DRIFT=""
if [ "${ICMG_NO_DRIFT_CHECK:-0}" != "1" ]; then
    if icmg drift check "$PROMPT" 2>/tmp/icmg-drift.out >/dev/null; then
        : # exit 0 = no conflict
    else
        DRIFT=$(cat /tmp/icmg-drift.out 2>/dev/null)
    fi
    rm -f /tmp/icmg-drift.out 2>/dev/null
fi

[ -z "$HITS" ] && [ -z "$CTX" ] && [ -z "$SUGGEST" ] && [ -z "$DRIFT" ] && exit 0
MSG=""
[ -n "$DRIFT" ]   && MSG="${MSG}${DRIFT}\n\n"
[ -n "$HITS" ]    && MSG="${MSG}icmg memory hits (proactively surfaced):\n${HITS}\n\n"
[ -n "$CTX" ]     && MSG="${MSG}icmg context for ${FIRSTPATH}:\n${CTX}\n\n"
[ -n "$SUGGEST" ] && MSG="${MSG}${SUGGEST}\n"
jq -n --arg m "$MSG" '{
    hookSpecificOutput: {
        hookEventName: "UserPromptSubmit",
        additionalContext: $m
    }
}'
)BASH";

// Phase 40 T2: PreCompact hook — auto-snapshots session before /compact
// or auto-compaction wipes context. Runs `icmg session save auto-precompact-<ts>`.
static const char* PRECOMPACT_PY = R"PY(#!/usr/bin/env python3
"""icmg PreCompact hook (auto-installed by `icmg init`).

Snapshots active session via `icmg session save auto-precompact-<ts>` so
decisions and recall context survive Claude Code context compression.
"""
import json, shutil, subprocess, sys, time
def main():
    try: sys.stdin.read()
    except: pass
    icmg = shutil.which("icmg") or shutil.which("icmg.exe")
    if not icmg:
        print(json.dumps({"continue": True})); return 0
    tag = "auto-precompact-" + time.strftime("%Y%m%d-%H%M%S")
    try:
        subprocess.run([icmg, "session", "save", tag],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
        print(json.dumps({"continue": True,
                          "systemMessage": f"icmg snapshot: {tag}"}))
    except Exception:
        print(json.dumps({"continue": True}))
    return 0
if __name__ == "__main__": sys.exit(main())
)PY";

// Embedded sidecar — kept in sync with embed/icmg_embedder.py.
// `icmg init` drops this to ~/.icmg/embed/icmg_embedder.py so binary-only
// installs (where source tree isn't adjacent) can still find the sidecar.
static const char* EMBEDDER_PY = R"PY(#!/usr/bin/env python3
"""icmg embedder sidecar — auto-installed by `icmg init`.
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

### ABSOLUTE RULE — icmg FIRST, ALWAYS

**Before any native tool call (Read / Bash / Grep / Glob / WebFetch), STOP and check the decision tree below.** If an `icmg` command serves the same need, you MUST use icmg. No exceptions, no "small file" excuses, no "just this once."

Order of resolution for every action:

1. **Is there an icmg command for this?** → run it
2. **No icmg command?** → run native tool
3. **icmg command failed?** → diagnose with `icmg doctor` first; only fall back to native when icmg explicitly errors

This is enforced at hook level (strict mode auto-on). Native calls that have an icmg equivalent are blocked with a redirect message. Do not waste tokens trying native first — the hook will block, you'll re-issue via icmg, you've burned a turn.

**Common slip-ups that cost tokens:**
- Reading a big file with native Read instead of `icmg context <file>` → 80%+ saved
- `grep -r` instead of `icmg run grep ...` → unfiltered noise
- WebFetch instead of `icmg fetch <url>` → no cache, no reduce
- `cat large.log` instead of `icmg compress < large.log` → no glossary
- Running 3 reads sequentially instead of `icmg parallel` → 3-6× wall-clock loss

### CRITICAL: parallel-first rule

**If you have 2+ independent tasks (independent files, independent checks, independent recalls), ALWAYS run them via `icmg parallel`.** Do NOT run sequentially. This is non-negotiable — sequential runs waste wall-clock and miss the I/O parallelism win (3-6× speedup on typical paths).

```bash
# Wrong: sequential — waits each
icmg verify --command "ctest"
icmg verify --command "cmake --build build"
icmg run npm test

# Right: parallel — all run concurrently
icmg parallel \
    --task "icmg verify --command 'ctest'" \
    --task "icmg verify --command 'cmake --build build'" \
    --task "icmg run npm test"
```

Heuristic: if your next 2+ steps don't share a file write or depend on each other's output, use `icmg parallel`.

### Decision tree

| Want to | Use |
|---|---|
| **Run 2+ independent steps** | `icmg parallel --task "..." --task "..."` (default — see rule above) |
| Read a large file | `icmg context <file>` (graph + symbols + memory) |
| Find a function | `icmg graph symbol <Name>` (30 lines, not 800) |
| Trace impact | `icmg graph reverse-impact <Name> --depth 5` |
| Search code | `icmg run grep ...` (auto-filtered) |
| Recall past decision | `icmg recall "<query>"` |
| Paraphrase recall | `icmg recall "<query>" --semantic` |
| Start new task | `icmg pack "<task>"` (4KB context bundle) |
| Delegate to LLM | `icmg agent "<task>"` (pack→prompt→user-CLI) |
| Run noisy command | `icmg run <cmd>` (Tkil filter — 60-90% smaller) |
| Big git diff | `icmg diff-summary --ref HEAD~5` |
| Errored before? | `icmg explain "<error>"` |
| List directory | `icmg ls [path]` |
| Clone existing menu | `icmg parity <ref> <new>` (catch missed handlers) |
| Generate scaffold | `icmg template extract <ref> --save-as X` then `icmg template apply X --to <new>` |

**Auto-rewrite hook installed.** Raw `grep`, `node`, `cargo build`, `pytest`, etc. auto-redirect through `icmg run`. Bypass with `RAW=1 <cmd>`.

### Persist learnings (always)
- Fixed a bug? `icmg known-issue add "<pattern>" --fix "<resolution>"`
- Made a decision? `icmg store --topic decisions-<feature> "<rationale>"`
- Long-form rationale (post-mortem, ADR)? `icmg memoir add --title T --content-file F`

Full reference: run `icmg --help` or see https://github.com/ncmonx/icm-graph
<!-- icmg:end -->
)MD";

class InitCommand : public BaseCommand {
public:
    std::string name()        const override { return "init"; }
    std::string description() const override { return "Bootstrap project for icmg-aware AI agents (hooks + AGENTS.md)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg init [options]\n\n"
            "Sets up:\n"
            "  .claude/settings.local.json  — Bash-rewrite + Read-shrink hooks\n"
            "  .claude/hooks/icmg-*.sh      — bundled hook scripts\n"
            "  AGENTS.md                    — routing rules for AI agents\n"
            "  ~/.icmg/embed/icmg_embedder.py — embedder sidecar (semantic recall)\n\n"
            "Options:\n"
            "  --no-hooks      Skip .claude/ setup\n"
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

        // Global strict flag: ~/.icmg/strict.flag → enforce on every init/upgrade.
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

        int steps = 0;
        if (!no_hooks)    { steps += installHooks(root, force, strict_read); }
        if (!no_agents)   { steps += installAgents(root, force); }
        if (!no_embedder) { steps += installEmbedder(force); }

        if (!no_scan) {
            std::cout << "  graph scan: run `icmg graph scan` to populate symbol index\n";
            std::cout << "  embed:      run `icmg embed memory` (requires Python sentence-transformers)\n";
        }

        // Phase 74 T6: auto-enable self-protection on init (opt-out via --no-backup / --no-maintain).
        // First snapshot fires synchronously so user has immediate recovery point.
        if (!no_backup) {
            std::cout << "  backup:     enabling hourly auto-snapshot...\n";
            auto r1 = core::safeExecShell("icmg backup snapshot --note init", false, 30000);
            if (r1.exit_code != 0)
                std::cerr << "    [warn] initial snapshot failed (continuing)\n";
            auto r2 = core::safeExecShell("icmg backup auto-on --interval 1h", false, 15000);
            if (r2.exit_code != 0) {
                std::cerr << "    [warn] auto-on failed — run manually: icmg backup auto-on\n";
            } else {
                std::cout << "    OK: scheduler armed (every 1h)\n";
            }
        }
        if (!no_maintain) {
            std::cout << "  maintain:   enabling 6h hygiene...\n";
            auto r = core::safeExecShell("icmg maintain auto-on --interval 6h", false, 15000);
            if (r.exit_code != 0) {
                std::cerr << "    [warn] maintain auto-on failed — run manually: icmg maintain auto-on\n";
            } else {
                std::cout << "    OK: scheduler armed (every 6h)\n";
            }
        }
        if (!no_mirror) {
            std::cout << "  mirror:     enabling 15m dual-mirror...\n";
            auto r1 = core::safeExecShell("icmg mirror sync", false, 30000);
            if (r1.exit_code != 0)
                std::cerr << "    [warn] initial mirror sync failed (continuing)\n";
            auto r2 = core::safeExecShell("icmg mirror auto-on --every 15m", false, 15000);
            if (r2.exit_code != 0) {
                std::cerr << "    [warn] mirror auto-on failed — run manually: icmg mirror auto-on\n";
            } else {
                std::cout << "    OK: failover armed (refresh every 15m)\n";
            }
        }
        if (!hasFlag(args, "--no-sentinel")) {
            std::cout << "  sentinel:   enabling 15m watchdog...\n";
            auto r = core::safeExecShell("icmg sentinel auto-on --every 15m", false, 15000);
            if (r.exit_code != 0) {
                std::cerr << "    [warn] sentinel auto-on failed — run manually: icmg sentinel auto-on\n";
            } else {
                std::cout << "    OK: watchdog armed (heavy/idle + disk/audit checks)\n";
            }
        }
        if (!hasFlag(args, "--no-auto-upgrade")) {
            std::cout << "  upgrade:    enabling daily shadow check...\n";
            auto r = core::safeExecShell("icmg shadow-upgrade auto-on --every 24h", false, 15000);
            if (r.exit_code != 0) {
                std::cerr << "    [warn] shadow-upgrade auto-on failed — run manually: icmg shadow-upgrade auto-on\n";
            } else {
                std::cout << "    OK: shadow auto-upgrade armed (daily check)\n";
            }
        }

        std::cout << "\nDone. " << steps << " file(s) written.\n"
                  << "Restart your AI agent to pick up new hooks.\n";
        return 0;
    }

private:
    int installHooks(const fs::path& root, bool force, bool strict_read = false) {
        int n = 0;
        fs::create_directories(root / ".claude" / "hooks");

        // Drop hook scripts.
        n += writeFile(root / ".claude" / "hooks" / "icmg-bash-rewrite.sh", BASH_REWRITE_SH, force);
        n += writeFile(root / ".claude" / "hooks" / "icmg-shrink-read.sh", SHRINK_READ_SH, force);
        // Phase 45 T3: cap-output PostToolUse hook (auto-shrink Bash >50KB).
        n += writeFile(root / ".claude" / "hooks" / "icmg-cap-output.sh", CAP_OUTPUT_SH, force);
        // Phase 40 T2: PreCompact auto-snapshot.
        n += writeFile(root / ".claude" / "hooks" / "icmg-precompact-snapshot.py", PRECOMPACT_PY, force);
        // Phase 51 T2: caveman SessionStart hook.
        n += writeFile(root / ".claude" / "hooks" / "icmg-caveman-prompt.sh", CAVEMAN_PROMPT_SH, force);
        // Phase 71: UserPromptSubmit auto-recall + suggest compress.
        n += writeFile(root / ".claude" / "hooks" / "icmg-prompt-recall.sh", PROMPT_RECALL_SH, force);

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
        json pre_array = json::array({
            {
                {"matcher", "Bash"},
                {"hooks",   json::array({
                    {{"type", "command"}, {"command",
                        std::string("[ -f .claude/hooks/icmg-bash-rewrite.sh ] && ") +
                        (strict_read ? "ICMG_STRICT_BASH=1 " : "") +
                        "bash .claude/hooks/icmg-bash-rewrite.sh || exit 0"}}
                })}
            },
            {
                {"matcher", "Read"},
                {"hooks",   json::array({
                    {{"type", "command"}, {"command",
                        std::string("[ -f .claude/hooks/icmg-shrink-read.sh ] && ") +
                        (strict_read
                            ? "ICMG_SHRINK_STRICT=1 ICMG_SHRINK_THRESHOLD=20000 ICMG_SHRINK_EXCLUDE='\\.(cs|ts|tsx|js|jsx|py|rb|go|rs|cpp|hpp|c|h|java|kt|sql)$' "
                            : "") +
                        "bash .claude/hooks/icmg-shrink-read.sh || exit 0"}}
                })}
            }
        });
        // Phase 56 T1: WebFetch redirect to `icmg fetch` (cache + reduce → 70-90% off).
        // Strict mode hard-denies; soft mode emits suggestion but allows.
        if (strict_read) {
            pre_array.push_back({
                {"matcher", "WebFetch"},
                {"hooks", json::array({
                    {{"type", "command"}, {"command",
                        "INPUT=$(cat); URL=$(echo \"$INPUT\" | jq -r '.tool_input.url // empty' 2>/dev/null); "
                        "[ -z \"$URL\" ] && exit 0; "
                        "HOMED=\"${USERPROFILE:-${HOME:-/tmp}}\"; mkdir -p \"$HOMED/.icmg\" 2>/dev/null; "
                        "printf '{\"ts\":%s,\"hook\":\"webfetch-strict\",\"target\":%s,\"reason\":\"WebFetch denied\"}\\n' \"$(date +%s)\" \"$(printf '%s' \"$URL\" | jq -Rs .)\" >> \"$HOMED/.icmg/strict-denials.jsonl\" 2>/dev/null || true; "
                        "jq -n --arg u \"$URL\" '{hookSpecificOutput:{hookEventName:\"PreToolUse\",permissionDecision:\"deny\",permissionDecisionReason:(\"STRICT mode: use `icmg fetch \" + $u + \"` (cached + reduced, 70-90% token saving). Bypass: icmg strict off.\")}}'; "
                        "exit 2"}}
                })}
            });
        }
        // (PreToolUse assigned later after Phase 70 Edit hook appended.)
        // Phase 45 T3: PostToolUse:Bash — auto-shrink huge outputs (>8KB).
        // Phase 67 T4: PostToolUse:Edit — capture user fixes to AI-emitted code.
        // Phase 70: PostToolUse:Glob/Grep/WebFetch + universal cap for any
        // tool result >8KB. Coverage extension targeting outside-icmg waste.
        cfg["hooks"]["PostToolUse"] = json::array({
            {
                {"matcher", "Bash|PowerShell"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "[ -f .claude/hooks/icmg-cap-output.sh ] && bash .claude/hooks/icmg-cap-output.sh || exit 0"}}
                })}
            },
            {
                {"matcher", "Edit"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "INPUT=$(cat); echo \"$INPUT\" | jq -c '.tool_input | {old_string, new_string, file_path}' 2>/dev/null | icmg correction capture 2>/dev/null || true"}}
                })}
            },
            // Phase 70: Glob/Grep cap — both produce path/line lists that
            // accumulate fast. Cap to top 50 entries.
            {
                {"matcher", "Glob|Grep"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command",
                      "INPUT=$(cat); "
                      "OUT=$(echo \"$INPUT\" | jq -r '.tool_response.output // .tool_response.content // empty' 2>/dev/null); "
                      "LINES=$(printf '%s' \"$OUT\" | wc -l); "
                      "[ \"$LINES\" -lt 50 ] && exit 0; "
                      "HEAD=$(printf '%s' \"$OUT\" | head -50); "
                      "MSG=$(printf '%s\\n... [%d total entries; first 50 shown — refine query for fewer] ...\\n' \"$HEAD\" \"$LINES\"); "
                      "jq -n --arg m \"$MSG\" '{hookSpecificOutput:{hookEventName:\"PostToolUse\",additionalContext:$m}}'"}}
                })}
            },
            // Phase 70: WebFetch result cap — even after icmg fetch reduce,
            // direct WebFetch can return large pages. Cap to 4KB.
            {
                {"matcher", "WebFetch"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command",
                      "INPUT=$(cat); "
                      "OUT=$(echo \"$INPUT\" | jq -r '.tool_response.content // .tool_response.output // empty' 2>/dev/null); "
                      "SZ=${#OUT}; "
                      "[ \"$SZ\" -lt 4096 ] && exit 0; "
                      "HEAD=$(printf '%s' \"$OUT\" | head -c 4000); "
                      "MSG=$(printf '%s\\n... [WebFetch capped from %d to 4KB; use icmg fetch for cached + reduced] ...\\n' \"$HEAD\" \"$SZ\"); "
                      "jq -n --arg m \"$MSG\" '{hookSpecificOutput:{hookEventName:\"PostToolUse\",additionalContext:$m}}'"}}
                })}
            },
            // Phase 73: PostToolUse:Read — auto-pipe big Read output through
            // `icmg compress` so compression layer fires on real content, not
            // just `icmg pack` calls. Hook already capped Read to 30 lines via
            // PreToolUse, so output ≥1KB here means real content worth compressing.
            {
                {"matcher", "Read"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command",
                      "INPUT=$(cat); "
                      "OUT=$(echo \"$INPUT\" | jq -r '.tool_response.content // .tool_response.output // empty' 2>/dev/null); "
                      "SZ=${#OUT}; "
                      "[ \"$SZ\" -lt 1024 ] && exit 0; "
                      "command -v icmg >/dev/null 2>&1 || exit 0; "
                      "COMPRESSED=$(printf '%s' \"$OUT\" | icmg compress --threshold 256 2>/dev/null); "
                      "[ -z \"$COMPRESSED\" ] && exit 0; "
                      "OZ=${#OUT}; CZ=${#COMPRESSED}; "
                      "[ \"$CZ\" -ge \"$OZ\" ] && exit 0; "
                      "MSG=$(printf 'Read output auto-compressed (%dB → %dB). Glossary inline; aliases match original tokens.\\n%s' \"$OZ\" \"$CZ\" \"$COMPRESSED\"); "
                      "jq -n --arg m \"$MSG\" '{hookSpecificOutput:{hookEventName:\"PostToolUse\",additionalContext:$m}}'"}}
                })}
            }
        });
        cfg["hooks"]["PreToolUse"] = pre_array;

        // Phase 51 T2: SessionStart caveman directive.
        cfg["hooks"]["SessionStart"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "[ -f .claude/hooks/icmg-caveman-prompt.sh ] && bash .claude/hooks/icmg-caveman-prompt.sh || exit 0"}}
                })}
            }
        });

        // Phase 71: UserPromptSubmit — auto-recall memory + auto-context for
        // path mentions + suggest compress on large pastes. Forces 4 core
        // features into Claude awareness every turn.
        cfg["hooks"]["UserPromptSubmit"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "[ -f .claude/hooks/icmg-prompt-recall.sh ] && bash .claude/hooks/icmg-prompt-recall.sh || exit 0"}}
                })}
            }
        });

        // Phase 40 T2: PreCompact hook — snapshot session before /compact.
        // Phase 67 T5: also distill session into memory primer for next session.
        // Phase 75: also re-inject AGENTS.md ABSOLUTE RULE + pinned drift anchors
        //           so post-compact context retains rule scaffolding.
        cfg["hooks"]["PreCompact"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "python3 .claude/hooks/icmg-precompact-snapshot.py 2>/dev/null || python .claude/hooks/icmg-precompact-snapshot.py 2>/dev/null || true"}},
                    {{"type", "command"},
                     {"command", "INPUT=$(cat); echo \"$INPUT\" | jq -r '.transcript // empty' 2>/dev/null | icmg distill session 2>/dev/null || true"}},
                    {{"type", "command"},
                     {"command", "RULE='ABSOLUTE RULE — icmg FIRST. Before any native Read/Bash/Grep/Glob/WebFetch, check icmg equivalent. Strict hooks block redundant native calls. Pinned decisions: $(icmg drift list --limit 5 2>/dev/null | tail -n +3 | head -10)'; jq -n --arg m \"$RULE\" '{hookSpecificOutput:{hookEventName:\"PreCompact\",additionalContext:$m}}'"}}
                })}
            }
        });
        // Phase 67 T3: Stop hook auto-distills assistant message into memory.
        // Phase 67 T32: also pipes raw JSON to compliance check-thinking →
        // logs violation when thinking section > 80 words while caveman ON.
        // Phase 57: tool-budget reset on each Stop (per-turn counter).
        cfg["hooks"]["Stop"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "INPUT=$(cat); echo \"$INPUT\" | jq -r '.message.content[]?.text // empty' 2>/dev/null | icmg distill auto --min-len 100 2>/dev/null || true"}},
                    {{"type", "command"},
                     {"command", "INPUT=$(cat); echo \"$INPUT\" | icmg compliance check-thinking --max-words 80 2>/dev/null || true"}},
                    {{"type", "command"},
                     {"command", "icmg tool-budget reset 2>/dev/null || true"}}
                })}
            }
        });
        std::ofstream f(sp);
        f << cfg.dump(2) << "\n";
        std::cout << "  + .claude/settings.local.json (hooks installed)\n";
        ++n;
        return n;
    }

    int installAgents(const fs::path& root, bool force) {
        fs::path ap = root / "AGENTS.md";
        std::string existing;
        if (fs::exists(ap)) {
            std::ifstream f(ap); std::ostringstream s; s << f.rdbuf(); existing = s.str();
        }
        const std::string start = "<!-- icmg:start -->";
        const std::string end   = "<!-- icmg:end -->";

        if (existing.find(start) != std::string::npos) {
            // Replace existing block.
            auto a = existing.find(start);
            auto b = existing.find(end);
            if (b == std::string::npos || b < a) return 0;
            std::string before = existing.substr(0, a);
            std::string after  = existing.substr(b + end.size());
            std::ofstream f(ap);
            f << before << AGENTS_BLOCK << after;
            std::cout << "  + AGENTS.md (icmg block updated)\n";
            return 1;
        }

        std::ofstream f(ap, std::ios::app);
        if (!existing.empty() && existing.back() != '\n') f << "\n";
        f << "\n" << AGENTS_BLOCK;
        std::cout << "  + AGENTS.md (icmg block appended)\n";
        return 1;
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
        // Drop sidecar to ~/.icmg/embed/icmg_embedder.py — findScript() picks
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
