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
)BASH";

static const char* SHRINK_READ_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. PreToolUse:Read — inject `icmg summarize` for large files.
set -uo pipefail
INPUT=$(cat)
FILE=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty' 2>/dev/null)
[[ -z "$FILE" ]] && exit 0
[[ ! -f "$FILE" ]] && exit 0
THRESHOLD="${ICMG_SHRINK_THRESHOLD:-60000}"
INC="${ICMG_SHRINK_INCLUDE:-}"
EXC="${ICMG_SHRINK_EXCLUDE:-}"
[[ -n "$INC" ]] && ! echo "$FILE" | grep -qE "$INC" && exit 0
[[ -n "$EXC" ]]   && echo "$FILE" | grep -qE "$EXC" && exit 0
SIZE=$(stat -c%s "$FILE" 2>/dev/null || stat -f%z "$FILE" 2>/dev/null || echo 0)
[[ "$SIZE" -lt "$THRESHOLD" ]] && exit 0
SUMMARY=$(icmg summarize "$FILE" 2>/dev/null || true)
[[ -z "$SUMMARY" ]] && exit 0

# ICMG_SHRINK_STRICT=1 -> hard-deny + redirect (force agent to icmg context).
# Default soft mode just injects summary as additionalContext (Read still runs).
if [[ "${ICMG_SHRINK_STRICT:-0}" = "1" ]]; then
    jq -n --arg f "$FILE" --arg sz "$SIZE" --arg s "$SUMMARY" '{
        hookSpecificOutput: {
            hookEventName: "PreToolUse",
            permissionDecision: "deny",
            permissionDecisionReason: ("File " + $f + " is " + $sz + " bytes. Use `icmg context " + $f + "` (graph + symbols + memory) or `icmg graph symbol <Name>` for one symbol body. Bypass: set ICMG_SHRINK_STRICT=0 in hook env.\n\nicmg summarize:\n" + $s)
        }
    }'
    exit 2
fi

jq -n --arg s "$SUMMARY" --arg f "$FILE" --arg sz "$SIZE" '{
    hookSpecificOutput: {
        hookEventName: "PreToolUse",
        additionalContext: ("Large file " + $f + " (" + $sz + " bytes). icmg summarize:\n\n" + $s + "\n\nProceed with full Read or use `icmg context " + $f + "` for filtered slice.")
    }
}'
exit 0
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

This project uses **icmg** for token-efficient code navigation. Prefer icmg over raw bash.

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
        bool force       = hasFlag(args, "--force");
        bool strict_read = hasFlag(args, "--strict-read");

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
        // Phase 40 T2: PreCompact auto-snapshot.
        n += writeFile(root / ".claude" / "hooks" / "icmg-precompact-snapshot.py", PRECOMPACT_PY, force);

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
        cfg["hooks"]["PreToolUse"] = json::array({
            {
                {"matcher", "Bash"},
                {"hooks",   json::array({
                    {{"type", "command"}, {"command", "[ -f .claude/hooks/icmg-bash-rewrite.sh ] && bash .claude/hooks/icmg-bash-rewrite.sh || exit 0"}}
                })}
            },
            {
                {"matcher", "Read"},
                {"hooks",   json::array({
                    // --strict-read sets ICMG_SHRINK_STRICT=1 + lower threshold so
                    // big files (>20KB) get hard-deny + redirect suggestion
                    // (Use icmg context X). Source-code extensions still allowed
                    // via ICMG_SHRINK_EXCLUDE so Edit-by-line workflow works.
                    {{"type", "command"}, {"command",
                        std::string("[ -f .claude/hooks/icmg-shrink-read.sh ] && ") +
                        (strict_read
                            ? "ICMG_SHRINK_STRICT=1 ICMG_SHRINK_THRESHOLD=20000 ICMG_SHRINK_EXCLUDE='\\.(cs|ts|tsx|js|jsx|py|rb|go|rs|cpp|hpp|c|h|java|kt|sql)$' "
                            : "") +
                        "bash .claude/hooks/icmg-shrink-read.sh || exit 0"}}
                })}
            }
        });
        // Phase 40 T2: PreCompact hook — snapshot session before /compact.
        cfg["hooks"]["PreCompact"] = json::array({
            {
                {"hooks", json::array({
                    {{"type", "command"},
                     {"command", "python3 .claude/hooks/icmg-precompact-snapshot.py 2>/dev/null || python .claude/hooks/icmg-precompact-snapshot.py 2>/dev/null || true"}}
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
