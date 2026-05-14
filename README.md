<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icm-graph/total)](https://github.com/ncmonx/icm-graph/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/commits/main)
[![tests](https://img.shields.io/badge/tests-63%2F63%20passing-brightgreen)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-28-blueviolet)](#)
[![commands](https://img.shields.io/badge/CLI%20commands-99%2B-blue)](#)
[![license](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/ncmonx/icm-graph/badge)](https://securityscorecards.dev/viewer/?uri=github.com/ncmonx/icm-graph)
[![OpenSSF Best Practices](https://img.shields.io/cii/level/12818?label=OpenSSF%20Best%20Practices)](https://www.bestpractices.dev/projects/12818)
[![sponsor](https://img.shields.io/badge/sponsor-GitHub-ea4aaa?logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![ko-fi](https://img.shields.io/badge/Ko--fi-tip-ff5e5b?logo=ko-fi)](https://ko-fi.com/ncmonx)

> **Stop burning tokens. Stop losing context. Ship faster.**

A single binary that makes Claude Code, Cursor, and every other AI coding assistant **70–90% cheaper** to run — without dumbing them down.

If you've ever watched 30K tokens evaporate on a single file read, paid for "thinking" you didn't need, or re-explained the same project context after `/clear` for the fifth time today — this is for you.

---

## 🟢 Status

| Aspect | Today |
|---|---|
| **Platform** | Windows x64 — daily-driven, battle-tested |
| **Stability gate** | Pre-1.0 — 63/63 tests gate every release; integrity hashes on every artifact |
| **Production-stable milestone** | Reached when Linux x64 + macOS arm64/x64 binaries ship alongside Windows |
| **Self-healing** | Snapshot + dual-mirror + audit log + auto-recovery built into the binary itself |

Windows users today: **safe to depend on for solo + small-team workflows**. The hard part — token economics, integrity, recovery — already works in production. Cross-platform parity is the last gate before the `v1.0` tag.

> 🛣️ **Roadmap:** multi-platform release plan at `docs/plans/2026-05-14-multi-platform-release.md` (8 tasks, GitHub Actions matrix scaffold ready). First cross-platform release ships as `v1.0.0`.

---

## ⚡ What's new in v0.54.0

| Feature | What changed |
| --- | --- |
| **In-process Stop hook** | `icmg hook stop` consolidates 4 prior subprocess forks (distill auto / fail sync-denials / compliance check-thinking / tool-budget reset) into one binary call — ~150ms saved per session end |
| **In-process PreCompact hook** | `icmg hook precompact` collapses snapshot + distill-session + ABSOLUTE-RULE emit into a single fork — ~100-150ms saved per compaction boundary |
| **Consolidated hook config** | `icmg init` now emits compact Stop/PreCompact blocks — fewer shell layers, faster startup |
| **Db prepared-statement LRU (v0.53.2)** | `core/db.{hpp,cpp}` caches up to 50 prepared statements; hot-path queries skip re-prepare entirely |
| **Build pipeline auto-tune (v0.53.3)** | CMake auto-detects `ccache` with smoke-test guard; opt-in `-DICMG_USE_LLD=ON` / `-DICMG_UNITY_BUILD=ON`; cold build ~7min (was ~20min), incremental `--target icmg` ~12s |

```bash
icmg init --force              # reinstall consolidated hooks
icmg hook stop                 # invoked automatically on session end
icmg hook precompact           # invoked automatically before compaction
cmake -B build -DICMG_UNITY_BUILD=ON   # faster cold builds (opt-in)
```

---

## 🔍 What's new in v0.53.0

| Feature | What changed |
| --- | --- |
| **BFS graph traversal (4 new cmds)** | `icmg graph path <from> <to>` — shortest dependency path; `graph layers <file> [--reverse]` — deps grouped by distance; `graph neighbors <file>` — direct 1-hop deps; `graph common <a> <b>` — shared upstream dependencies |
| **Impact edge-type filter** | `icmg graph impact <file> --edge-type imports,calls` — filter to specific edge types only |
| **Multi-source impact** | `icmg graph impact --all <f1> <f2>` — union impact from multiple files in one call |
| **DOT graph export** | `icmg graph impact <file> --format dot` — Graphviz DOT output, pipe to `dot -Tsvg` |
| **WAL bloat fix (critical)** | `wal_autocheckpoint` 1000→100 pages — prevents SQLite WAL growing to GBs under concurrent hook writers |
| **CMD popup fix (Windows)** | Task Scheduler PS1 launcher uses `ProcessStartInfo.CreateNoWindow=$true` — reliably hides console on Win11 |
| **Full command reference on init** | `icmg init` injects ~50-command reference table into `AGENTS.md` — Claude knows every command from session 1, no help queries needed |
| **Graph BFS savings tracking** | `icmg savings` includes Graph BFS row — tracks `path`/`layers`/`neighbors`/`common` calls, estimates 2000 tokens saved per call |

```bash
icmg graph path src/auth.cpp src/db.cpp          # shortest dependency path
icmg graph layers src/core/db.cpp --reverse      # who depends on this, by distance level
icmg graph neighbors src/main.cpp                # direct 1-hop dependencies
icmg graph common src/auth.cpp src/api.cpp       # shared upstream dependencies
icmg graph impact src/db.cpp --edge-type imports # filter impact by edge type
icmg graph impact src/db.cpp --format dot | dot -Tsvg > impact.svg
icmg graph impact --all src/auth.cpp src/db.cpp  # union impact of two files
icmg init --force                                 # reinstall hooks + inject full command ref
```

## 🛠️ What's new in v0.53.1

| Fix | What changed |
| --- | --- |
| **Zone glob Windows paths** | Zone patterns (`src/cli/**`) now match Windows absolute paths — nodes no longer mis-tag to `default` on Windows |
| **Doctor caveman loop** | `icmg doctor` no longer re-warns about caveman hook on every run after auto-reinstall |

```bash
icmg zone rebuild   # re-tag any mis-classified nodes after upgrade (Windows users)
```

---

## 🚀 What's new in v0.52.0

| Feature | What changed |
| --- | --- |
| **Cross-session awareness** | `icmg session claim/clear/list` writes `~/.icmg/active-work.json`; `icmg wake-up` shows concurrent tasks from other sessions on the same machine |
| **Anti-pattern sync** | `icmg fail sync-denials` converts `~/.icmg/strict-denials.jsonl` hook violations → fail memory nodes; runs automatically on session Stop |
| **Wake-up SessionStart hook** | `icmg init` now installs a SessionStart hook that injects `icmg wake-up` briefing at the start of every AI session |
| **Caveman ultra auto-on** | `icmg init` creates `~/.icmg/caveman.flag=ultra` if absent — caveman mode active on first run, no manual step |
| **Routing table complete** | AGENTS.md routing table expanded from 15 → 39 commands; topic prefix convention (`plan:`, `bug:`, `decisions-`) documented for deterministic recall |
| **Memory read perf** | `PRAGMA mmap_size=256MB` + `PRAGMA page_size=4096` in all DB opens — read-heavy workloads skip syscall overhead |

```bash
icmg session claim "working on auth refresh"   # register task for cross-session visibility
icmg wake-up                                    # now shows Active sessions block
icmg fail sync-denials                          # convert hook violations to fail memory
icmg init --force                               # gets wake-up hook + caveman flag
```

---

## ⚡ What's new in v0.51.0

| Feature | What changed |
|---|---|
| **Destructive-op guard** | `icmg run` intercepts `rm -rf`, `DROP TABLE`, `git push --force` etc. — prompts `[y/N]` before executing; `--yes` bypasses |
| **Version staleness check** | Startup checks GitHub for latest release (24h cached); warns at lag≥3, soft-blocks `init`/`graph` at lag≥10 |
| **Wake-up per-user filter** | `icmg wake-up` now filters decisions/fixes by `created_by` — multi-user machines no longer mix context |
| **Hook token efficiency** | `hook userprompt` now respects 4KB budget cap, BM25≥0.15 threshold, adaptive depth, 300s context cache, 1-hop BFS graph expansion |
| **CMD popup suppression** | `exec_utils` subprocess launch adds `STARTF_USESHOWWINDOW\|SW_HIDE` — no console flicker on all Windows configs |
| **Init security** | `icmg init` warns on OneDrive/Dropbox path + sets owner-only permissions on `.icmg/` |

```bash
icmg run --yes rm -rf build/        # skip destructive prompt for safe dirs
icmg wake-up                        # now shows [user: email] header
icmg upgrade                        # resolve version staleness warnings
```

---

## What it does (single-page tour)

```
                ┌───────────────────────┐
   YOUR TASK ──▶│  icmg pack "<task>"   │── filtered context bundle
                └────────────┬──────────┘     (memory + graph + diff)
                             │
                             ▼
                ┌───────────────────────┐
                │   28 MCP tools        │── Claude Code / Cline / Continue
                │   recall, store, …    │
                └────────────┬──────────┘
                             │
                             ▼
                ┌───────────────────────┐
                │  Hooks intercept      │── Read 100-line cap
                │  Read / Bash / Edit   │── Bash 8KB cap, ANSI strip
                │  Glob / Grep / Web    │── Glob top-50, WebFetch 4KB
                └────────────┬──────────┘
                             │
                             ▼
                ┌───────────────────────┐
                │   Auto-compress       │── reversible glossary on
                │   (≥3KB pack)         │   pack output
                └────────────┬──────────┘
                             │
                             ▼
              SAVINGS DASHBOARD + RECEIPTS + COMPLIANCE TRACKING
```

| 😤 Pain | ✅ Icmg fix |
| :--- | :--- |
| Big files inflate every prompt | Surgical context bundles — only what the task actually needs |
| Noisy command output drowns the model | Output filtering tuned per command type (ANSI strip, dedup ×N, blank collapse) |
| Same bug solved twice | Persistent memory that surfaces past fixes when they apply |
| `/clear` wipes hard-won context | Snapshots + auto-distill of session decisions |
| Models "think" 8K tokens for a one-line rename | Intent-aware directives + caveman mode kills thinking outright |
| Re-sending the same project preamble every turn | Long-lived prompt-cache markers — pay once, reuse cheap |
| 30K tokens of logs / diffs / dumps | Lossless context compression with reversible round-trips |
| AI keeps trying the same broken approach | Anti-pattern memory (`icmg fail`) — failures become guardrails |
| AI "forgets" your CLAUDE.md instructions | Hard-enforcement hooks block disallowed reads/fetches |
| Native `Read` bypasses everything | Read cap-and-allow hook caps at 30 lines + icmg-context overlay |
| Same task already solved in another project | `icmg cross-recall` federates memory across all registered projects |
| DB grows unbounded over months | `icmg cron install` autopilots weekly prune (Windows schtasks / POSIX cron) |
| Wrong zone wastes BM25 IDF | Auto-zone detect from task keywords (10 zones); no manual `--zone X` |

Each one is a few percent. Stack them and you get the headline number.

---

## Quick start

```bash
# Build
cmake -B build && cmake --build build

# Or grab the release binary
# https://github.com/ncmonx/icm-graph/releases

# Bootstrap a project (installs the right hooks for your AI agent)
icmg init

# Try one of these
icmg pack "fix the login bug"          # surgical context bundle
icmg run npm test                      # noise-filtered build output
icmg compress < big.log                # cut tokens on dumps + diffs
icmg pack "rename foo.ts"              # auto-think on; thinking off when task is simple
icmg context src/auth.ts --symbol parseToken  # one function body, 80%+ cut vs full file
icmg run --stream npm test             # real-time output + noise filter summary at end
icmg context src/auth.ts --lines 60-95 # surgical read; replaces native Read offset/limit
icmg fail store "jwt refresh" "X" "Y"  # record failed approach so AI doesn't repeat
icmg savings                           # see what you saved (console / --html)
```

That's it. Nothing else to configure.

---

## 📊 Headline numbers (measured)

| Layer | Saving | Mechanism |
| :--- | :---: | :--- |
| 📂 **Big-file Read** | **60–80% smaller** | File slice, 100-line cap |
| 🔨 **Build/test logs** | **80–95% smaller** | `icmg run` filter pipeline |
| 🗄️ **SQL/table dumps** | **95–99% smaller** | Per-tool shrink strategy |
| 🧠 **Thinking overhead** | **50–90% off** | `--no-think` + caveman mode |
| 📌 **Stable preamble** | **90% off** | Prompt-cache markers |
| 🔁 **Repeat queries** | **100% off** | `tool_call_cache` (5 min TTL) |
| 📦 **Bulk operations** | **50% off** | Anthropic Batch API emit |
| 🌐 **HTML/PDF fetch** | **70–90% off** | `icmg fetch` reduce + cache |
| 🖼️ **Screenshot OCR** | **90–95% off** | `icmg ingest` pytesseract |
| ♻️ **Read repeat-dedup** | **~100% on dup** | 30-min sliding window |
| 🔄 **Pack on-repeat** | **60–97% smaller** | `icmg pack --diff` |
| 🔬 **Symbol-slice context** | **80%+ per lookup** | `icmg context --symbol` (one fn, not whole file) |
| 🚿 **Filter ANSI/dedup** | **30–60%** on noisy CLI | npm/cargo/pnpm output |
| 📋 **File copy no-output** | **97% per write** | `icmg copy --from` (zero output tokens) |
| 🗣️ **Caveman mode** | **~75% on responses** | Fragment-style directive |

> 💰 **Combined on a typical turn: ≈ 85–95% — ~$0.10 saved per non-trivial Claude turn, hundreds per month at scale.**

---

## 📈 Coverage dashboard

`icmg savings` shows exactly where the savings came from, with receipts and cost breakdown:

```
icmg savings — last 30 days
================================================================

  Command filter (icmg run)             67 calls       16.6K  →  16.2K   (2% saved)
  Compression  (icmg compress)           1 calls       5.0K   →  5.0K    (0% saved)
  Thinking     (--no-think)             21 calls       31.5K  →  7.5K    (76% saved)
  Pack receipts  (memory+graph)         15 calls       9.2K   →  9.2K
  Strict denials (read/web/bash)         8 calls       12.0K  →  0       (100% saved)
  Fetch cache    (icmg fetch)            3 hits        7.5K   →  0       (100% saved)
  Image OCR cache(icmg ingest)           2 hits        4.0K   →  0       (100% saved)
----------------------------------------------------------------
  TOTAL                                117 calls       86K    →  38K     (56% saved)

Cost without icmg: $0.50  (input $0.03 / output $0.47)
Cost with    icmg: $0.13  (input $0.02 / output $0.11)
You saved:         $0.37  (63.6%)

Real session tokens: 2195587  (icmg-covered 86K = 4%, outside 2.1M)
Strict-mode denials in window: 8
  → each block redirected agent to icmg context/fetch
```

`Real session tokens` row reads the live Claude transcript — surfaces the gap between icmg-instrumented ops and actual context-window fill.

---

## ✨ Highlights

| | Feature | Details |
| :---: | :--- | :--- |
| 🔋 | **One binary** | ~30 MB Windows .exe — no node_modules, venv, or Docker |
| 🏠 | **Local-first** | Per-project SQLite — never phones home |
| 🔌 | **MCP server** | 28 tools (recall, store, graph, sync, fetch, batch…) — Claude Code, Cline, Continue, anything MCP |
| 🗺️ | **Project federation** | `icmg cross-recall` — "this was done in project X" across all registered projects |
| 🤖 | **Autopilot hygiene** | `icmg cron install` — weekly prune (auto/session/fail/correction rotation), zero-touch |
| 🎯 | **Auto-zone** | Pack infers zone from keywords (10 zones: auth/db/graph/imem/tkil/mcp/ui/cli/hooks/compress) |
| 📊 | **Dashboard** | `icmg serve` → [http://127.0.0.1:8080/](http://127.0.0.1:8080/) — graph, memory, savings, knowledge browser |
| 🌳 | **AST-aware** | tree-sitter for C/C++/Python/TypeScript/Go/Rust/Java/C#/Ruby/Bash/Kotlin/Lua — exact symbols, line ranges, body hashes |
| 👥 | **Team-friendly** | Memory + graph share via git-tracked JSONL snapshots |
| 🖼️ | **Image-aware** | Local OCR for screenshots — 90%+ token cut vs vision API |
| 🔒 | **Hard enforcement** | Hooks block native Read/WebFetch when icmg has it; leash hook blocks all raw Bash |
| 🔧 | **Self-repair** | DLL sha256 auto-rollback, lock recovery, pending-restart across versions |
| 🛡️ | **Self-protection** | Atomic snapshots (24h/7d/4w/6m pyramidal) + dual-mirror ping-pong + 7-stage graph integrity |
| ⚡ | **Hot-context cache** | Re-issue same `icmg context/compress` → ~5 ms cache hit (15–30× cold→hot), self-invalidating on edit |
| 🧹 | **Self-maintenance** | `icmg maintain run` — HEAVY/IDLE detection → prune chain → bounded growth, zero-touch |
| 📍 | **Drift gate** | `icmg drift pin` — pinned decisions get 10× recall boost; contradictions flagged before model commits |
| 👁️ | **Sentinel watchdog** | 15-min health checks; auto-prunes disk/cache/audit; loop-safe (halt at ≥3 reactions/hour) |
| 🔄 | **Shadow auto-upgrade** | Daily background GitHub poll → sha256-verified → atomic swap on next invocation. Chrome-style |
| 📋 | **Audit trail** | Chain-signed log of every backup/restore/failover/sentinel reaction — tamper-detectable |
| 🔬 | **Symbol slice** | `icmg context --symbol <Name>` — one function body, not the whole module. 80%+ token cut |
| 🚫 | **Session dedup** | Recall suppresses already-returned nodes — no repeat flooding. `--no-dedup` to override |
| 🌊 | **Live stream filter** | `icmg run --stream` — real-time line-by-line output + filter summary appended at end |
| 📚 | **Context graph** | CLAUDE.md auto-imported as BM25-searchable nodes — ~93% less context on session start |
| 🎓 | **Skill store** | Skill `.md` files indexed; matching skills auto-suggested per prompt via hook |
| ⚖️ | **Rule enforcement** | DB-stored rules evaluated at 2–5 ms (10× faster than hook checks); 500-line file guard |
| 🗂️ | **Knowledge browser** | `icmg knowledge list/add/edit/delete` + HTML dashboard at `icmg serve /knowledge` |

---

## 🗺️ When to use which command

| Situation | Command |
| :--- | :--- |
| 🚀 Starting a task | `icmg pack "<task>"` — one bundle, not 5–10 reads |
| 📂 Need a single file | `icmg context <file>` — surgical, not full Read |
| 📏 Need lines 60–95 | `icmg context <file> --lines 60-95` — replaces Read offset/limit |
| 🔨 Any noisy command | `icmg run <cmd>` — filtered output |
| 🔍 PR review | `icmg diff-summary` — symbol-grouped, not raw diff |
| 📦 Big text input | `icmg compress` — cut tokens, reversible |
| 🧠 Past decisions | `icmg recall "<query>"` — surfaces what you already learned |
| ❌ Failed approach | `icmg fail store/recall` — anti-pattern memory |
| 🗺️ Same task in another project | `icmg cross-recall "<prompt>"` — federate across projects |
| 🤖 Auto-prune old memory | `icmg cron install` — Win schtasks / POSIX cron, zero-touch |
| 🔒 AI ignored CLAUDE.md | `icmg strict on` — hooks enforce rules at harness level |
| 🤖 Full LLM pipeline | `icmg agent "<task>"` — pack + cache + directives + retry |
| 💰 Bulk Anthropic | `icmg batch --task ...` — 50% off via Batch API |
| 🌐 Download URL | `icmg fetch <url>` — reduced + cached (70–90% off) |
| 🖼️ Screenshot | `icmg ingest screenshot.png` — OCR text-only payload |
| 👥 Team share | `icmg sync init/push/pull` — git-tracked JSONL |
| 📈 Audit savings | `icmg savings` — console / `--html` / `--json` |
| 📊 Real session tokens | `icmg context-budget` — covers ALL sources |
| 🆕 What changed | `icmg whats-new` — release notes after `update` |
| 📡 Visual graph | `icmg serve` — embedded HTTP dashboard |
| **DB safety net** | `icmg backup snapshot` / `icmg backup restore latest` / `icmg backup restore-from <file>` — atomic, schema-checked; cross-project restore supported |
| **Instant failover** | `icmg mirror failover` — swaps in valid mirror in seconds |
| **Self-clean heavy/idle DB** | `icmg maintain run` — auto-detects state, chains prune + integrity |
| **Repair broken graph** | `icmg graph integrity --fix` — 7-stage check + targeted repair |
| **Inspect cache layer** | `icmg cache stats / list / prune` — see what's hot |
| **Pin a decision** | `icmg drift pin --topic X --stance Y` — pinned memory wins recall 10× |
| **Check prompt drift** | `icmg drift check "<prompt>"` — surfaces conflicts with pinned anchors |
| **Watchdog health** | `icmg sentinel run` — auto-prunes disk/cache; halt-safe loop guard |
| **Background upgrade** | `icmg shadow-upgrade check` — daily auto-poll; pin/rollback supported |
| **Audit trail** | `icmg repair-history tail / verify` — chain-signed event log |

Run `icmg --help` for the full list of 82+ subcommands. Each has its own `--help`.

> More layers in progress. Concrete designs not published.

---

## Install

**Windows (recommended):** download the latest release, drop `icmg.exe` somewhere on `PATH`. The bundled DLLs live next to it. Done.

**Build from source:**

```bash
git clone https://github.com/ncmonx/icm-graph
cd icm-graph
cmake -B build && cmake --build build
# Optional capabilities (turn on what you want)
cmake -B build -DICMG_USE_TREESITTER=ON -DICMG_USE_ONNX=ON
```

Optional capabilities are exactly that — optional. Default build runs everywhere with zero external dependencies beyond a C++17 compiler.

---

## 📅 How it pays off in practice

After a few days of use you'll notice:

| Milestone | What changes |
| :---: | :--- |
| **Day 1** | Sessions get visibly longer before `/compact` fires |
| **Day 3** | Recurring questions answer themselves from memory |
| **Day 7** | Big PR reviews stop blowing the context window |
| **Day 14** | Cache hit-rate climbs as you settle into patterns |
| **Day 30** | Your monthly Claude bill stops scaring you |

Memory recall sharpens over time. Snapshot restore gets faster. Compression learns your codebase's repeated terms. **The system compounds.**

---

## Architecture (one screen)

```
                       ┌─────────────────────────────────────┐
                       │           icmg.exe (single binary)   │
                       └─────────────────────────────────────┘
                                       │
        ┌──────────────────────────────┼──────────────────────────────┐
        │                              │                              │
   ┌─────────┐                  ┌──────────────┐               ┌──────────────┐
   │   CLI   │                  │  MCP Server  │               │ HTTP Server  │
   │ 70+ cmd │                  │  (stdio)     │               │ :8080 read   │
   └────┬────┘                  └──────┬───────┘               └──────────────┘
        │                              │
        ▼                              ▼
   ┌────────────────────────────────────────────┐
   │   Core: SQLite WAL · BM25 · embedder       │
   │         tree-sitter · pytesseract sidecar  │
   └────────────────────────────────────────────┘
        │
        ▼
   ┌────────────────────────────────────────────┐
   │  ~/.icmg/global.db        — registry        │
   │  <proj>/.icmg/data.db     — per-project     │
   │  ~/.icmg/*.flag           — caveman/strict  │
   │  ~/.icmg/*-log.{jsonl|txt}— receipts        │
   └────────────────────────────────────────────┘
```

---

## Security

Local-first design with explicit boundaries:

- **Update integrity:** every release ships a SHA256 sidecar per binary. `update --apply` verifies before swap; mismatch auto-rollbacks. Bypass via `--skip-verify`.
- **Per-DLL SHA256:** 6 bundled DLLs (onnxruntime / providers-shared / tree-sitter / wasmtime / zstd / winpthread) verified after install; auto-restore from `.bak` on mismatch.
- **URL sanitization:** `icmg fetch` validates URLs against a shell-metacharacter blocklist (`"$\;|&<>` newlines control chars) before any shell-out.
- **HTTPS-only** for self-update + fetch.
- **Parameterized SQL** queries throughout (no SQL injection on store/recall/import).
- **No telemetry phoned home** — the binary makes network calls only when you invoke `update`, `fetch`, `embed` (Python sidecar), or `whats-new`.
- **Per-project DB** is plaintext SQLite. If you store secrets as memory, treat the DB file as sensitive (filesystem permissions remain the boundary).
- **Hooks** modify `.claude/settings.local.json` only on `icmg init`. Review before opt-in.

Open caveats:
- Image OCR runs Python `pytesseract` + `Pillow` subprocess; respect those projects' CVE history when ingesting untrusted images.
- MCP stdio is unauthenticated (local-only threat model).
- DB encryption opt-in still in design (key-recovery UX risk).

---

## Self-repair

icmg is designed to recover from common failure modes on its own. The trade-off: recovery takes a few extra seconds in exchange for safety.

| Situation | What happens |
|---|---|
| Update target binary locked (Windows) | Detached helper waits for the running process to exit, then performs the swap on the next invocation — no manual restart needed |
| Update integrity mismatch (sha256) | Aborts before swap, keeps the previous binary in place |
| DLL bundle drift after upgrade | Per-DLL sha256 verify catches mismatches; auto-rollback restores `.bak` |
| Stale lockfile from killed process | Auto-detected via PID liveness probe and cleaned up |
| Pending upgrade interrupted | Marker file resumes the swap on the next `icmg` invocation in any new terminal |
| Hook scripts drift after upgrade | `update --apply` re-runs `init --install-hooks --force` automatically |
| Telemetry tables grow unbounded | `icmg memory prune-telemetry` reclaims space; `prune-old --topic 'auto:%'` rotates auto-grown topics |
| DB schema lag | Migrations apply automatically on next open; backward-compatible |
| Project graph stale | `icmg context` auto-scans single file inline if not yet indexed |
| TS/MD/JSON files missing from graph | Auto-detected by extension and indexed on next access |
| **DB corruption detected** | `icmg mirror failover` swaps in newest valid mirror in seconds; primary quarantined for forensics; audit-logged |
| **No mirror available** | `icmg backup restore latest` rolls back to most recent atomic snapshot; auto-undo created first |
| **Project copied to new location (DB missing)** | `icmg backup restore-from .icmg/backups/<id>.db` — restores from explicit file; creates `.icmg/` dir if absent; `--no-undo` skips pre-restore snapshot |
| **DB heavy (>100MB or >50K rows)** | `icmg maintain run` chains telemetry-prune → topic-aged prune → decay → consolidate → integrity check |
| **Project idle (no activity >24h)** | `icmg maintain run --idle-mode` soft-deletes auto/session/cache rows below importance 2; graph + pinned memory untouched |

**Always-on protection (auto-armed on `icmg init` and every upgrade):**

| Layer | Cadence | Disk cost | Purpose |
|---|---|---|---|
| Snapshot history | hourly | pyramidal (24h/7d/4w/6m) | Time-travel recovery |
| Dual mirror | 15 min | 2× live | Instant failover |
| Hygiene maintain | 6 h | n/a | Bounded growth |
| Graph integrity | within maintain | n/a | Drift detection |

Most recovery paths take 1–3 seconds. A few (network re-fetch on integrity mismatch, helper-script wait for exit) can take 10–30 seconds. Safety is prioritized over speed — every recovery preserves the previous good state via `.bak` files so manual rollback is always available.

Run `icmg health` any time to confirm everything is in order.

---

## Honest limits

```
✓  Windows is primary target. Linux / macOS work but tested less.
✓  Opinionated. Fight the conventions and you fight the tool.
✓  Won't make a bad prompt good — makes a good prompt cheap.
✓  Some optional capabilities require one-time DLL download (binary tells you).
✓  Compression is semantic glossary — model still must understand aliases inline.
✓  Real session coverage starts ~50%; climbs as more hooks fire on subsequent ops.
```

See [CHANGELOG.md](CHANGELOG.md) for the full ship history (35+ releases, atomic per-task).

---

## Fuel the next mile

icmg is built by one person, MIT-grade rigor, Apache-2.0 license, free forever. The math of running this kind of project is honest:

| Cost line | What it funds |
|---|---|
| 🖥️ Build + CI server | Multi-platform matrix builds, security scans, reproducible release pipeline |
| 🤖 AI agent subscriptions | Claude / Cursor / Cline access used to dogfood + accelerate development |
| 📦 Future infra | macOS code-signing cert (Apple Developer ID), package-mirror CDN as users grow |

If icmg has saved you tokens that would have cost more than a coffee, you're welcome to:

[![Sponsor on GitHub](https://img.shields.io/badge/💚_Sponsor-GitHub-ea4aaa?style=for-the-badge&logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![Tip on Ko-fi](https://img.shields.io/badge/☕_Tip-Ko--fi-ff5e5b?style=for-the-badge&logo=ko-fi)](https://ko-fi.com/ncmonx)

No pressure, no nags, no paywalled features. Every line of icmg stays open-source regardless. Support just routes the dev pipeline straight at the next bottleneck — usually multi-platform CI minutes, ANN index work, or a faster recall path.

---

## License

Apache 2.0. Use it however you want. Attribution appreciated, not required.

See [LICENSE](LICENSE) and [NOTICE](NOTICE).

---

## Other docs

- `CHANGELOG.md` — every shipped release, outcome-first
- `AGENTS.md` — how to wire icmg into your AI agent's instruction set
- `COMMANDS.md` — full CLI reference (70+ commands)
- `CLAUDE.md` — Claude Code-specific notes
- `docs/plans/` — phase plans + open backlog (Phase 69)

---

**TL;DR:** stop paying full price for AI coding sessions you don't need to. Drop `icmg` in your `PATH`, run `icmg init`, get back to shipping.

```
                            ◆ icemage
                  context · memory · graph
              62/62 tests · 28 MCP · 99+ cmds
            cross-project federation · autopilot hygiene
```
