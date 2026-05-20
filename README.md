<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icm-graph/total)](https://github.com/ncmonx/icm-graph/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/commits/main)
[![ctest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/ncmonx/7d6a2efa9d6191e28ff3f6a26e6ba7c7/raw/ctest.json)](https://github.com/ncmonx/icm-graph-src/actions)
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

## 🟢 Status — v1.0.0 production-stable

| Aspect | Today |
|---|---|
| **Platforms** | Windows x64 + Linux x64 — prebuilt binaries on every release |
| **macOS arm64** | Source builds cleanly (verified via prior CI); no prebuilt binary yet — see install section |
| **Stability gate** | 111/111 tests gate every release; sha256 sidecar on every binary |
| **Self-healing** | Snapshot + dual-mirror + audit log + auto-recovery built into the binary itself |
| **Hook overhead** | ~5-10 ms per session-boundary event (daemon RPC + zero-fork in-process runners) |

**v1.0.0 means:** stable wire formats, stable on-disk schema (migrations only forward), stable CLI surface, stable MCP tool names. No more breaking changes on minor bumps. Source-level work is complete across Win + Linux + macOS.

---

## 🛠 v1.18.1 — Hotfix: exec_client race + `icmg init` speedup

- **`exec_client` mutex pre-check + start sentinel**: eliminates the N-client race during fan-out (`update --apply`, parallel CLI calls) that left 30+ stuck pre-init `icmg-core.exe` processes and caused Claude tool calls to hang at 120s timeout.
- **`update --apply` cascade fixed**: `ICMG_NO_AUTOSPAWN=1` set at 3 fan-out sites so children no longer cascade-spawn services.
- **`icmg init` speedup**: claudemd/plan/skill sub-imports wrapped with `ICMG_NO_AUTOSPAWN=1` + reduced timeouts (20s/15s/30s → 10s/8s/15s). No more 1-minute first-init stall.

ctest 111/111. Drop-in. No schema or CLI surface change.

---

## 🛠 v1.18.0 — Service self-healing + popup-killer broadened + prefetch + observability + skill completion

- **Service self-healing**: `service status` PID-validated (no more stale-pidfile false positives). `exec_client` auto-spawns service detached when pipe dead + service PID dead. Popup-killer thread auto-recovers.
- **Popup-killer broadened**: accept Win11 `TaskDialogClass` + `DirectUIHWND*` + `WS_POPUP` style fallback. Same 100ms scan.
- **Prefetch + observability**: `core::prefetch_cache` warms hot context_nodes + skill manifest at service start. `core::query_cache` adds 5min TTL BM25 result cache (foundation). `icmg metrics` shows cache hit-rates + service health. `icmg whatchanged` reports memory delta since last stamp.
- **Skill completion**: `icmg skill stats` (per-skill size + total). `icmg skill suggest <prompt>` (BM25 intent predict, top-N).
- Session boundary: `session-inject` resets dedup + turn_cache.

ctest 111/111. Drop-in. Restart service after upgrade.

---

## 🛠 v1.17.0 — Hook scripts use bash `[[ ]]` keyword + TDD backlog burn (111/111)

- **Hook scripts resilient**: all bundled hooks migrated from POSIX `[ ]` (external `/usr/bin/[`) → bash `[[ ]]` (always builtin). Fixes `/usr/bin/[: cannot execute binary file` on users with broken MSYS coreutils.
- **ctest 108 → 111**: new suites `test_turn_cache`, `test_inject_dedup`, `test_session_inject`.

Drop-in. `icmg init --force` regenerates hook scripts.

---

> 📜 **Older releases:** see [`CHANGELOG.md`](CHANGELOG.md) for v1.16.x and earlier.

---

## 🎬 What it does (single-page tour)

Every token your agent burns goes through **five sequential cuts** — each one shaves a few percent. Stacked, they hit the 85–95% headline.

```
   ┌──────────────────────────────────────────────────────────────────────────┐
   │                          YOUR TASK                                       │
   └──────────────────────────────┬───────────────────────────────────────────┘
                                  │
                  ① BUNDLE        ▼
   ┌──────────────────────────────────────────────────────────────────────────┐
   │  icmg pack "<task>"  →  memory recall + graph BFS + diff slice           │
   │  ≈ 4 KB targeted context  (vs 30+ KB of "read every file" Claude does)   │
   └──────────────────────────────┬───────────────────────────────────────────┘
                                  │
                  ② SERVE         ▼
   ┌──────────────────────────────────────────────────────────────────────────┐
   │  28 MCP tools  →  Claude Code · Cline · Continue · Cursor · anything MCP │
   │  recall · store · graph · sync · fetch · batch · drift · compliance     │
   └──────────────────────────────┬───────────────────────────────────────────┘
                                  │
                  ③ INTERCEPT     ▼
   ┌──────────────────────────────────────────────────────────────────────────┐
   │  Hooks gate every native tool call before tokens are billed              │
   │  Read 100-line cap · Bash 8 KB cap · ANSI strip · Glob top-50 · 4 KB Web │
   └──────────────────────────────┬───────────────────────────────────────────┘
                                  │
                  ④ COMPRESS      ▼
   ┌──────────────────────────────────────────────────────────────────────────┐
   │  Auto-compress packs ≥ 3 KB  →  reversible glossary inline               │
   │  Aliases collide with original tokens — lossless round-trip              │
   └──────────────────────────────┬───────────────────────────────────────────┘
                                  │
                  ⑤ ACCOUNT       ▼
   ┌──────────────────────────────────────────────────────────────────────────┐
   │  SAVINGS DASHBOARD  ·  per-call receipts  ·  compliance & drift tracking │
   │  $$$ saved per turn, audited and reproducible                            │
   └──────────────────────────────────────────────────────────────────────────┘
```

### 🩹 The pain we kill, in three categories

**1. Context bloat — the silent killer**

| Pain | Icmg fix |
| :--- | :--- |
| Big files inflate every prompt | Surgical context bundles — only what the task actually needs |
| 30 K tokens of logs / diffs / dumps | Lossless context compression with reversible round-trips |
| Models "think" 8 K tokens for a one-line rename | Intent-aware directives + caveman mode kill thinking outright |
| Re-sending the same preamble every turn | Long-lived prompt-cache markers — pay once, reuse cheap |
| Noisy command output drowns the model | Output filtering per command type (ANSI strip, dedup ×N, blank collapse) |

**2. Memory amnesia — the bug you solved last week**

| Pain | Icmg fix |
| :--- | :--- |
| Same bug solved twice | Persistent memory that surfaces past fixes when they apply |
| `/clear` wipes hard-won context | Snapshots + auto-distill of session decisions |
| AI keeps trying the same broken approach | Anti-pattern memory (`icmg fail`) — failures become guardrails |
| Same task already solved in another project | `icmg cross-recall` federates memory across all registered projects |
| Wrong zone wastes BM25 IDF | Auto-zone detect from task keywords (10 zones); no manual `--zone X` |

**3. Discipline drift — when the AI ignores its own rules**

| Pain | Icmg fix |
| :--- | :--- |
| AI "forgets" your CLAUDE.md instructions | Hard-enforcement hooks block disallowed reads/fetches |
| Native `Read` bypasses everything | Read cap-and-allow hook caps at 30 lines + icmg-context overlay |
| DB grows unbounded over months | `icmg cron install` autopilots weekly prune (Windows schtasks / POSIX cron) |
| Drift between pinned decisions and AI behavior | `icmg drift check` — surfaces conflicts before the model commits |

> 🧮 **Each row is a few percent. Stack them — that's the headline number.**

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

## 📈 Coverage dashboard — receipts, not vibes

Every saving is **logged, attributed, and reproducible**. Run `icmg savings` any time — see exactly which layer cut how many tokens and how many dollars that turned into.

```text
─────────────────────────────────────────────────────────────────────
  icmg savings — last 30 days
─────────────────────────────────────────────────────────────────────
  LAYER                          CALLS    BEFORE   →   AFTER   SAVED
─────────────────────────────────────────────────────────────────────
  Command filter (icmg run)        67    16.6 K   →   16.2 K    2 %
  Compression   (icmg compress)     1     5.0 K   →    5.0 K    0 %
  Thinking      (--no-think)       21    31.5 K   →    7.5 K   76 %
  Pack receipts (memory+graph)     15     9.2 K   →    9.2 K    —
  Strict denials (read/web/bash)    8    12.0 K   →    0       100 %
  Fetch cache    (icmg fetch)       3 ✓   7.5 K   →    0       100 %
  Image OCR cache (icmg ingest)     2 ✓   4.0 K   →    0       100 %
─────────────────────────────────────────────────────────────────────
  TOTAL                           117    86  K   →   38  K    56 %
─────────────────────────────────────────────────────────────────────
  💵 Cost without icmg : $0.50   (input $0.03 / output $0.47)
  💵 Cost with    icmg : $0.13   (input $0.02 / output $0.11)
  💰 You saved         : $0.37   (63.6%)
─────────────────────────────────────────────────────────────────────
  📊 Real session tokens : 2,195,587   (icmg-covered 86K = 4%)
  🚫 Strict-mode denials : 8           (each redirected agent to icmg)
─────────────────────────────────────────────────────────────────────
```

### What this view tells you at a glance

| Column | What it means |
| :--- | :--- |
| **CALLS** | How many times that layer was hit — frequency drives the compound effect |
| **BEFORE → AFTER** | Token count Claude would have seen vs what it actually saw |
| **SAVED** | Hard percentage of that layer's tokens that never left your machine |
| **Real session tokens** | Read straight from the live Claude transcript — surfaces the gap between instrumented ops and total context-window fill |
| **Strict-mode denials** | Every native call that was blocked + redirected to icmg — proof the rules are enforced |

> 💡 **Try it after one day:** `icmg savings --html` opens a richer dashboard. `icmg savings --json` feeds your own scripts. No telemetry — the data never leaves your machine.

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

**Windows (recommended):** download the latest release, drop `icmg.exe` somewhere on `PATH`. Bundled `libwinpthread-1.dll` lives next to it. Done.

**Linux x64 — build from source** *(no prebuilt binary as of v0.59.0; solo-dev cost-saving)*:

```bash
# One-time deps (Ubuntu/Debian)
sudo apt update && sudo apt install -y cmake ninja-build build-essential zlib1g-dev curl git
# Clone + build
git clone https://github.com/ncmonx/icm-graph
cd icm-graph
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DICMG_USE_ONNX=ON -DICMG_USE_TREESITTER=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure   # 111/111 should pass
sudo install -m755 build/icmg /usr/local/bin/
# ONNX runtime lib (if you enabled it) — put next to icmg or on LD_LIBRARY_PATH
sudo install -m644 third_party/onnxruntime/lib/libonnxruntime.so.* /usr/local/lib/
sudo ldconfig
```

On Windows, the same build works from a **WSL2 Ubuntu** shell — produces a native Linux ELF binary you can ship to Linux users.

**macOS (arm64 or Intel) — build from source:**

```bash
brew install cmake ninja zlib curl git
git clone https://github.com/ncmonx/icm-graph
cd icm-graph
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DICMG_USE_ONNX=ON -DICMG_USE_TREESITTER=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
sudo install -m755 build/icmg /usr/local/bin/
# ONNX dylib next to binary or on DYLD_LIBRARY_PATH
sudo cp -RP third_party/onnxruntime/lib/libonnxruntime*.dylib /usr/local/lib/
# First run only — Gatekeeper override for unsigned local build
xattr -d com.apple.quarantine /usr/local/bin/icmg 2>/dev/null || true
```

**Optional capabilities** (`ICMG_USE_ONNX` for semantic recall, `ICMG_USE_TREESITTER` for AST symbol extractors) auto-fetch their dependencies via CMake (`curl` required). Default-OFF build runs everywhere with zero external dependencies beyond a C++17 compiler — useful for slim deployments where regex-extractor + BM25-only recall is enough.

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

## 🏛️ Architecture — one binary, four surfaces, zero services

No daemon to babysit, no Docker, no `node_modules`. **One ~30 MB executable** exposes four entry points and persists everything in local SQLite. That's the whole architecture.

```
   ┌──────────────────────────────────────────────────────────────────────────┐
   │              🟢  icmg.exe   —  single static binary  (~30 MB)            │
   └──────────────────────────────────────────────────────────────────────────┘
                                       │
            ┌─────────────────┬────────┴────────┬──────────────────┐
            ▼                 ▼                 ▼                  ▼
   ┌────────────────┐ ┌──────────────┐ ┌──────────────┐ ┌────────────────────┐
   │  🖥️  CLI       │ │  🔌  MCP     │ │  🌐  HTTP    │ │  🛡️  Hook handler   │
   │   99+ subcmds  │ │   28 tools   │ │   :8080      │ │   Read/Bash/Web    │
   │   pack · run · │ │   stdio JSON │ │   dashboard  │ │   intercept before │
   │   compress …   │ │   recall …   │ │   read-only  │ │   tokens are billed│
   └────────┬───────┘ └──────┬───────┘ └──────┬───────┘ └──────────┬─────────┘
            │                │                │                    │
            └────────────────┴────────┬───────┴────────────────────┘
                                      ▼
   ┌──────────────────────────────────────────────────────────────────────────┐
   │  🧠  CORE  (in-process — no IPC, no socket round-trip)                   │
   │                                                                          │
   │   SQLite WAL  ·  BM25 lexical  ·  ONNX embedder  ·  tree-sitter AST     │
   │   pytesseract sidecar  ·  rule-daemon (B3-B6 hook RPC, optional)         │
   └──────────────────────────────────┬───────────────────────────────────────┘
                                      │
                                      ▼
   ┌──────────────────────────────────────────────────────────────────────────┐
   │  📁  STORAGE  (local-first — never leaves your machine)                  │
   │                                                                          │
   │   ~/.icmg/global.db          → cross-project registry, receipts, cron   │
   │   <proj>/.icmg/<proj>.db     → per-project graph + memory + zones       │
   │   ~/.icmg/*.flag             → caveman / strict / sentinel state        │
   │   ~/.icmg/*-log.{jsonl,txt}  → chain-signed audit trail                 │
   │   ~/.icmg/snapshots/         → 24h/7d/4w/6m pyramidal backups           │
   └──────────────────────────────────────────────────────────────────────────┘
```

### Why this shape matters

| Property | What it buys you |
| :--- | :--- |
| **Single binary** | No service discovery, no port conflicts, no version skew between client/server |
| **In-process core** | Hook callbacks resolve in ~2–5 ms — no socket round-trip, no JSON-RPC overhead |
| **Per-project DB** | Snapshot, diff, restore, and ship to teammates as a single file |
| **Audit trail** | Chain-signed events — tamper-detectable, replayable, auditable |
| **Zero phone-home** | Network calls only happen when you ask: `update`, `fetch`, `embed`, `whats-new` |

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

## FAQ

**How does icmg compare to Aider's repo-map / Cursor's `@Codebase`?**
Both build a project index. icmg's difference is per-task bundles (BM25 + cosine over a persistent SQLite memory) instead of a single static map, plus output filtering + reversible glossary compression on every hook event. Designed to compound across all 7 layers, not just context.

**Does icmg send my code anywhere?**
No. Zero telemetry, zero cloud calls, zero account required. Everything is local SQLite. `icmg config show` lists every external URL the binary would ever hit (release-update check + opt-in WebFetch cache only). You can grep your own memory store.

**Will icmg work with `<MyClient>` (Claude Code / Cursor / Cline / Continue / LM Studio)?**
Anything that speaks MCP works. `icmg --mcp-server` exposes 28 tools over stdio JSON-RPC. Claude Code also gets the hook integration for free (`icmg init` writes `.claude/settings.local.json`).

**Does it support local LLMs (Ollama / LM Studio / llama.cpp)?**
Yes — through the same MCP server route. The recall ranking is tokenizer-agnostic; the receipts table's cost coefficients are Claude-tuned but you can override per model in `~/.icmg/config.json`.

**macOS arm64?**
Source builds cleanly. Prebuilt binary blocked on Apple hardware availability — if you have a Mac and want to ship a PR, the CMake flow is the same as Linux.

**Why C++ and not Rust / Go?**
Single binary + sub-10 ms hook latency + cold-start matters. C++17 + SQLite + statically-linked MinGW gave the best size/speed tradeoff. Rust v2 may happen if compile-time pain accumulates.

**How does the "popup-free" daemon actually work?**
`icmg service` is a single Windows logon-trigger task launched via `wscript.exe //B` (GUI subsystem, no console flash). It calls every internal command via `Registry<BaseCommand>` directly — zero subprocess fork. Replaces the 5 per-project schtasks pre-v1.1.0 used. See [v1.1.1](https://github.com/ncmonx/icm-graph/releases/tag/v1.1.1).

**Where do I report bugs / request features?**
[GitHub issues](https://github.com/ncmonx/icm-graph/issues). Real-world reproductions with `icmg savings --json` output get triaged fastest.

---

## Star history

<a href="https://star-history.com/#ncmonx/icm-graph&Date">
  <img src="https://api.star-history.com/svg?repos=ncmonx/icm-graph&type=Date" alt="Star history" width="600"/>
</a>

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
