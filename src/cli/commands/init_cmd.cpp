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
# Phase 79: delegated to in-process `icmg hook pretooluse-read`.
# Replaces the previous shell chain (jq + stat + icmg summarize + icmg context)
# with a single forked process. Saves ~100-300ms per Read on cold-start overhead.
set -uo pipefail
[ "${ICMG_NO_READ_HOOK:-0}" = "1" ] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
exec icmg hook pretooluse-read
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
# Phase 81: try daemon IPC first (~5ms); fall back to direct spawn (~360ms).
# Daemon must be running: `icmg daemon start`.
set -uo pipefail
[ "${ICMG_NO_PROMPT_HOOK:-0}" = "1" ] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0

# Read hook input JSON from stdin.
INPUT=$(cat)

# If jq available and daemon running: extract prompt, send via IPC (~5ms).
if command -v jq >/dev/null 2>&1; then
    PROMPT=$(printf '%s' "$INPUT" | jq -r '.prompt // empty' 2>/dev/null)
    if [ -n "$PROMPT" ]; then
        RESULT=$(icmg daemon client hook.userprompt --param "prompt=$PROMPT" 2>/dev/null)
        if [ -n "$RESULT" ]; then
            printf '%s' "$RESULT"
            exit 0
        fi
    fi
fi

# Fallback: direct spawn (~360ms).
printf '%s' "$INPUT" | exec icmg hook userprompt
)BASH";

// Stop hook — reminds to log workflow decisions when session had git activity.
static const char* WFLOG_STOP_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on session Stop.
# Reminds to save workflow decisions when session had code changes.
command -v icmg >/dev/null 2>&1 || exit 0
HAS_CHANGES=false
if command -v git >/dev/null 2>&1; then
    { git diff --quiet HEAD 2>/dev/null && git diff --cached --quiet 2>/dev/null; } || HAS_CHANGES=true
    if [ "$HAS_CHANGES" = "false" ]; then
        [ "$(git log --since='4 hours ago' --oneline 2>/dev/null | wc -l)" -gt 0 ] && HAS_CHANGES=true
    fi
fi
[ "$HAS_CHANGES" = "false" ] && exit 0
LAST=$(icmg wflog recent --limit 1 2>/dev/null | head -1)
[ -n "$LAST" ] && exit 0
jq -n '{hookSpecificOutput:{hookEventName:"Stop",additionalContext:"WFLOG: session had changes — log decisions: icmg wflog save --goal \"...\" --decisions \"...\""}}' 2>/dev/null || true
)BASH";

// v0.42.0: PreToolUse rule enforcement — call rule-daemon via `icmg rule-eval`.
// Blocks Read/Glob/Grep on files >500 lines; warns at >200 lines.
static const char* RULE_ENFORCE_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. PreToolUse:Read|Glob|Grep enforcement.
[ "${ICMG_NO_RULE_ENFORCE:-0}" = "1" ] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
INPUT=$(cat)
TOOL=$(echo "$INPUT" | jq -r '.tool_name // empty' 2>/dev/null)
FILE=$(echo "$INPUT" | jq -r '.tool_input.file_path // .tool_input.pattern // empty' 2>/dev/null)
[ -z "$TOOL" ] && exit 0
icmg rule-eval --tool "$TOOL" --file "$FILE" 2>/dev/null
EXIT=$?
[ $EXIT -eq 2 ] && exit 2  # BLOCK → hook deny
exit 0
)BASH";

// v0.42.0: SessionStart — inject hot context_nodes (always-on sections).
static const char* CONTEXT_SESSION_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on SessionStart.
# Pre-warms binary, clears session-reads dedup, injects hot context_nodes.
command -v icmg >/dev/null 2>&1 || exit 0
# Clear session dedup file — new session, fresh slate.
ICMG_HOME="${USERPROFILE:-$HOME}/.icmg"
[ -d "$ICMG_HOME" ] && > "$ICMG_HOME/session-reads.txt" 2>/dev/null || true
CONTENT=$(icmg context-node match "" --tier hot --top 5 --fmt plain 2>/dev/null)
[ -z "$CONTENT" ] && exit 0
jq -n --arg m "$CONTENT" '{hookSpecificOutput:{hookEventName:"SessionStart",additionalContext:$m}}'
)BASH";

// #1084: SessionStart wake-up injection.
static const char* WAKEUP_SESSION_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on SessionStart.
# Injects icmg wake-up briefing at start of every AI session.
set -uo pipefail
command -v icmg >/dev/null 2>&1 || exit 0
CONTENT=$(icmg wake-up 2>/dev/null) || true
[[ -z "$CONTENT" ]] && exit 0
jq -n --arg m "$CONTENT" '{hookSpecificOutput:{hookEventName:"SessionStart",additionalContext:$m}}'
)BASH";

// v0.42.0: UserPromptSubmit — BM25-match cold context_nodes + skill index per prompt.
static const char* CONTEXT_PROMPT_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on UserPromptSubmit.
# Injects relevant cold context_nodes + skill suggestions via BM25 match.
[ "${ICMG_NO_CONTEXT_HOOK:-0}" = "1" ] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
INPUT=$(cat)
PROMPT=$(echo "$INPUT" | jq -r '.message // .prompt // empty' 2>/dev/null)
[ -z "$PROMPT" ] && exit 0
COLD=$(icmg context-node match "$PROMPT" --tier cold  --top 3 --fmt plain 2>/dev/null)
SKILL=$(icmg context-node match "$PROMPT" --tier skill --top 2 --fmt plain 2>/dev/null)
COMBINED="$COLD"
[ -n "$SKILL" ] && COMBINED=$(printf '%s\n\n---\nSuggested skills:\n%s' "$COLD" "$SKILL")
[ -z "$COMBINED" ] && exit 0
jq -n --arg m "$COMBINED" '{hookSpecificOutput:{hookEventName:"UserPromptSubmit",additionalContext:$m}}'
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
| Delegate to LLM | `icmg agent "<task>"` (pack→prompt→user-CLI) |
| Run noisy command | `icmg run <cmd>` (Tkil filter — 60-90% smaller) |
| Big git diff | `icmg diff-summary --ref HEAD~5` |
| Errored before? | `icmg explain "<error>"` |
| Fetch URL with cache | `icmg fetch <url>` (cached + token-reduced) |
| Compress large output | `icmg compress` (pipe or `< file` — glossary output) |
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
| Caveman mode | `icmg caveman [on/off/status]` |
| Reinit hooks | `icmg init --force` |
| Batch cache writes | `icmg batch` (cut round-trips) |
| Release notes | `icmg whats-new` |
| Interactive REPL | `icmg chat` |
| File copy silent | `icmg copy --from <src> --to <dst>` |
| List directory | `icmg ls [path]` |
| Clone existing menu | `icmg parity <ref> <new>` (catch missed handlers) |
| Generate scaffold | `icmg template extract <ref> --save-as X` then `icmg template apply X --to <new>` |

**Auto-rewrite hook installed.** Raw `grep`, `node`, `cargo build`, `pytest`, etc. auto-redirect through `icmg run`. Bypass with `RAW=1 <cmd>`.

### Persist learnings (always)
- Fixed a bug? `icmg known-issue add "<pattern>" --fix "<resolution>"`
- Made a decision? `icmg store --topic decisions-<feature> "<rationale>"`
- Long-form rationale (post-mortem, ADR)? `icmg memoir add --title T --content-file F`
- Anti-pattern / failed approach? `icmg fail store "<task>" "<approach>" "<reason>"`

### Topic prefix conventions (makes recall deterministic)

| Type | Prefix | Example |
| --- | --- | --- |
| Plan | `plan:<feature>` | `icmg store --topic plan:auth-refresh "..."` |
| Bug | `bug:<symptom>` | `icmg store --topic bug:linker-error "..."` |
| Decision | `decisions-<area>` | `icmg store --topic decisions-db "..."` |
| Anti-pattern | use `icmg fail store` | see above |

Recall by prefix: `icmg recall "plan:auth"` or `icmg pack "<task>"` (auto BFS+BM25).

Full reference: run `icmg --help` or see https://github.com/ncmonx/icm-graph
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
| `icmg parallel --task "..." --task "..."` | Run 2+ tasks concurrently (3-6× speedup) |
| `icmg run <cmd>` | Run noisy command through Tkil filter |
| `icmg compress` | Pipe or `< file` — glossary-compressed output |
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
| `icmg agent "<task>"` | Delegate to LLM via pack→prompt |
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
| `icmg caveman [on/off/status]` | Toggle caveman mode |
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

        // T3: Cloud sync path detection — warn if .icmg/ would be synced.
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
                // Windows: icacls to remove inherited + grant owner-only.
                std::string p = icmg_dir.string();
                std::string cmd = "icacls \"" + p + "\" /inheritance:r /grant:r \"%USERNAME%\":F /T /Q";
                core::safeExecShell(cmd, true);
#endif
            }
        }

        int steps = 0;
        if (!no_hooks)    { steps += installHooks(root, force, strict_read); }
        if (!no_agents)   { steps += installAgents(root, force); }
        if (!no_embedder) { steps += installEmbedder(force); }

        if (!no_scan) {
            std::cout << "  graph scan: run `icmg graph scan` to populate symbol index\n";
            std::cout << "  embed:      run `icmg embed memory` (requires Python sentence-transformers)\n";
        }

        // v0.42.0 T-18: auto-start rule-daemon if not already running.
        {
            auto ping = core::safeExecShell("icmg rule-eval --tool PING 2>/dev/null", false, 3000);
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

        // v0.44.0: auto-import + slim CLAUDE.md(s) on init/upgrade.
        // Skips already-slim files; backup saved to .icmg/ before overwrite.
        {
            auto r = core::safeExecShell("icmg claudemd import --slim 2>&1", false, 20000);
            if (r.exit_code == 0) {
                std::cout << "  context-graph: CLAUDE.md imported + slimmed (backup in .icmg/)\n";
            } else {
                std::cout << "  context-graph: run `icmg claudemd import --slim` to slim CLAUDE.md\n";
            }
        }
        // v0.44.0: auto-import plan files (PROGRESS.md, docs/plans/) into context_nodes.
        {
            auto r = core::safeExecShell("icmg plan import 2>&1", false, 15000);
            if (r.exit_code == 0) {
                std::cout << "  plan-graph:    plan files imported to context_nodes\n";
            }
        }

        // v0.45.1: auto-index skill files so Claude discovers icmg features via BM25.
        {
            auto r = core::safeExecShell("icmg skill index 2>&1", false, 30000);
            if (r.exit_code == 0) {
                std::cout << "  skill-index:   skill files indexed for feature discovery\n";
            }
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

        // v0.45.0: auto-add icmg.exe to Windows Defender exclusion list.
        // Root cause: icmg spawns on every hook turn → Defender scans each spawn → CPU spike.
        // Non-fatal: skips silently if elevation unavailable (prints hint).
#ifdef _WIN32
        {
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
                    if (ec == 0)
                        std::cout << "  defender:   icmg.exe excluded (faster hook spawns)\n";
                    else
                        std::cout << "  defender:   exclusion needs elevation"
                                  << " — run `icmg init` as Administrator once to reduce Defender overhead\n";
                }
            }
        }
#endif

        std::cout << "\nDone. " << steps << " file(s) written.\n"
                  << "Restart your AI agent to pick up new hooks.\n";
        return 0;
    }

private:
    int installHooks(const fs::path& root, bool force, bool strict_read = false) {
        int n = 0;
        fs::create_directories(root / ".claude" / "hooks");

        // Drop hook scripts — always overwrite so upgrades fix stale/buggy hooks
        // (e.g. old versions with & background that caused WAL 65GB growth).
        // Users who need custom hooks should use --no-hooks and manage manually.
        n += writeFile(root / ".claude" / "hooks" / "icmg-bash-rewrite.sh", BASH_REWRITE_SH, true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-shrink-read.sh", SHRINK_READ_SH, true);
        // Phase 45 T3: cap-output PostToolUse hook (auto-shrink Bash >50KB).
        n += writeFile(root / ".claude" / "hooks" / "icmg-cap-output.sh", CAP_OUTPUT_SH, true);
        // Phase 40 T2: PreCompact auto-snapshot.
        n += writeFile(root / ".claude" / "hooks" / "icmg-precompact-snapshot.py", PRECOMPACT_PY, true);
        // Phase 51 T2: caveman SessionStart hook.
        n += writeFile(root / ".claude" / "hooks" / "icmg-caveman-prompt.sh", CAVEMAN_PROMPT_SH, true);
        // Auto-enable caveman ultra on init if flag absent (never overwrite existing level).
        {
            const char* h2 = std::getenv("HOME");
            if (!h2) h2 = std::getenv("USERPROFILE");
            if (h2) {
                fs::path cflag = fs::path(h2) / ".icmg" / "caveman.flag";
                if (!fs::exists(cflag)) {
                    fs::create_directories(cflag.parent_path());
                    std::ofstream ofs(cflag);
                    ofs << "ultra\n";
                    std::cout << "  caveman:    ultra mode enabled (~/.icmg/caveman.flag)\n";
                }
            }
        }
        // Phase 71: UserPromptSubmit auto-recall + suggest compress.
        n += writeFile(root / ".claude" / "hooks" / "icmg-prompt-recall.sh", PROMPT_RECALL_SH, true);
        // Stop hook: wflog reminder on session end when git has changes.
        n += writeFile(root / ".claude" / "hooks" / "icmg-wflog-stop.sh", WFLOG_STOP_SH, true);
        // v0.42.0: context graph injection hooks.
        n += writeFile(root / ".claude" / "hooks" / "icmg-context-session.sh", CONTEXT_SESSION_SH, true);
        n += writeFile(root / ".claude" / "hooks" / "icmg-context-prompt.sh",  CONTEXT_PROMPT_SH,  true);
        // #1084: wake-up briefing on SessionStart.
        n += writeFile(root / ".claude" / "hooks" / "icmg-wakeup-session.sh", WAKEUP_SESSION_SH, true);
        // v0.42.0: rule enforcement hook.
        n += writeFile(root / ".claude" / "hooks" / "icmg-rule-enforce.sh", RULE_ENFORCE_SH, true);

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
                {"matcher", "Read|Glob|Grep"},
                {"hooks",   json::array({
                    {{"type", "command"}, {"command",
                        "[ -f .claude/hooks/icmg-rule-enforce.sh ] && bash .claude/hooks/icmg-rule-enforce.sh || exit 0"}}
                })}
            },
            {
                {"matcher", "Read"},
                {"hooks",   json::array({
                    {{"type", "command"}, {"command",
                        std::string("[ -f .claude/hooks/icmg-shrink-read.sh ] && ") +
                        // Phase 79: force every Read through icmg context overlay.
                        // Removed source-ext exclusion that previously let .cpp/.ts/.cs
                        // bypass cap + overlay. Threshold lowered to 0 so even tiny
                        // files get the icmg-context hint. Cap at 30 lines covers
                        // Edit-anchor requirement. Opt-out: ICMG_NO_READ_FORCE=1.
                        "ICMG_READ_LIMIT=30 ICMG_SHRINK_THRESHOLD=0 " +
                        (strict_read ? "ICMG_SHRINK_STRICT=1 " : "") +
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
            // Phase 82 T2: PostToolUse:Read — graph-aware auto-reroute.
            // If file in graph: emit icmg context (structured, ~80% cut).
            // Fallback: icmg shrink. Threshold 4KB (ICMG_READ_THRESHOLD env).
            {
                {"matcher", "Read"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command",
                      "INPUT=$(cat); "
                      "FILE=$(echo \"$INPUT\" | jq -r '.tool_input.file_path // empty' 2>/dev/null); "
                      "OUT=$(echo \"$INPUT\" | jq -r '.tool_response.content // .tool_response.output // empty' 2>/dev/null); "
                      "SZ=${#OUT}; "
                      "THRESH=${ICMG_READ_THRESHOLD:-4096}; "
                      "[ \"$SZ\" -lt \"$THRESH\" ] && exit 0; "
                      "command -v icmg >/dev/null 2>&1 || exit 0; "
                      "if [ -n \"$FILE\" ] && [ -f \"$FILE\" ]; then "
                      "  CTX=$(icmg context \"$FILE\" --max-bytes 3000 2>/dev/null); "
                      "  if [ -n \"$CTX\" ]; then "
                      "    MSG=$(printf '[Read auto-rerouted to icmg context (%dB → structured)]\\n%s\\nHint: use `icmg context %s` directly.' \"$SZ\" \"$CTX\" \"$FILE\"); "
                      "    jq -n --arg m \"$MSG\" '{hookSpecificOutput:{hookEventName:\"PostToolUse\",additionalContext:$m}}'; "
                      "    exit 0; "
                      "  fi; "
                      "fi; "
                      "SHRUNK=$(printf '%s' \"$OUT\" | icmg shrink --threshold 0 2>/dev/null); "
                      "[ -z \"$SHRUNK\" ] && exit 0; "
                      "OZ=${#OUT}; SZ2=${#SHRUNK}; "
                      "[ \"$SZ2\" -ge \"$OZ\" ] && exit 0; "
                      "MSG=$(printf 'Read output shrunk (%dB → %dB). Use `icmg context %s` for structured output.\\n%s' \"$OZ\" \"$SZ2\" \"$FILE\" \"$SHRUNK\"); "
                      "jq -n --arg m \"$MSG\" '{hookSpecificOutput:{hookEventName:\"PostToolUse\",additionalContext:$m}}'"}}
                })}
            }
        });
        cfg["hooks"]["PreToolUse"] = pre_array;

        // Phase 51 T2: SessionStart caveman directive.
        // v0.42.0: also inject hot context_nodes at session start.
        cfg["hooks"]["SessionStart"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "[ -f .claude/hooks/icmg-caveman-prompt.sh ] && bash .claude/hooks/icmg-caveman-prompt.sh || exit 0"}},
                    {{"type", "command"},
                     {"command", "[ -f .claude/hooks/icmg-context-session.sh ] && bash .claude/hooks/icmg-context-session.sh || exit 0"}},
                    {{"type", "command"},
                     {"command", "[ -f .claude/hooks/icmg-wakeup-session.sh ] && bash .claude/hooks/icmg-wakeup-session.sh || exit 0"}}
                })}
            }
        });

        // Phase 71: UserPromptSubmit — auto-recall memory + auto-context.
        // v0.42.0: also inject BM25-matched cold context_nodes + skill suggestions.
        cfg["hooks"]["UserPromptSubmit"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "[ -f .claude/hooks/icmg-prompt-recall.sh ] && bash .claude/hooks/icmg-prompt-recall.sh || exit 0"}}
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
                     {"command", "command -v icmg >/dev/null 2>&1 || exit 0; exec icmg hook precompact"}}
                })}
            }
        });
        // v1.1.0 Task 6: PreToolUse hard-deny — denies raw shell / large Read /
        // powershell / cmd patterns when icmg has an equivalent. Per-session
        // bypass via ICMG_STRICT_BYPASS=1.
        cfg["hooks"]["PreToolUse"] = json::array({
            {
                {"matcher", "Bash|Read|Glob|Grep|WebFetch"},
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "command -v icmg >/dev/null 2>&1 || exit 0; exec icmg hook pretooluse"}}
                })}
            }
        });

        // v0.54.0: Stop hook consolidated into `icmg hook stop` single fork.
        // Replaces the 5-bash-command chain (distill / fail-sync / compliance /
        // tool-budget / wflog) — saves ~150-250ms per Stop event.
        cfg["hooks"]["Stop"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "command -v icmg >/dev/null 2>&1 || exit 0; exec icmg hook stop"}},
                    {{"type", "command"},
                     {"command", "[ -f .claude/hooks/icmg-wflog-stop.sh ] && bash .claude/hooks/icmg-wflog-stop.sh || exit 0"}}
                })}
            }
        });
        std::ofstream f(sp);
        f << cfg.dump(2) << "\n";
        std::cout << "  + .claude/settings.local.json (hooks installed)\n";
        ++n;

        // Install icmg-first enforcement into global ~/.claude/settings.json
        // so the rule applies across ALL projects, not just this one.
        n += installGlobalReadHook(force);

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
        const std::string hook_cmd =
            "python3 -c \"import json,sys; sys.stdin.read(); "
            "print(json.dumps({'hookSpecificOutput':{'hookEventName':'PreToolUse',"
            "'additionalContext':'ICMG-FIRST RULE: Before Read/Glob/Grep, use icmg first: "
            "icmg context <file>, icmg pack <task>, icmg graph symbol <Name>, "
            "icmg recall <query>, icmg graph search <query>. "
            "Direct tools only if icmg cannot cover it.'}}))\"";

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
