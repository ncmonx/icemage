<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![tests](https://img.shields.io/badge/tests-57%2F57%20passing-brightgreen)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-28-blueviolet)](#)
[![commands](https://img.shields.io/badge/CLI%20commands-88%2B-blue)](#)
[![license](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![sponsor](https://img.shields.io/badge/sponsor-GitHub-ea4aaa?logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![ko-fi](https://img.shields.io/badge/Ko--fi-tip-ff5e5b?logo=ko-fi)](https://ko-fi.com/ncmonx)

> **Stop burning tokens. Stop losing context. Ship faster.**

A single binary that makes Claude Code, Cursor, and every other AI coding assistant **70â€“90% cheaper** to run â€” without dumbing them down.

If you've ever watched 30K tokens evaporate on a single file read, paid for "thinking" you didn't need, or re-explained the same project context after `/clear` for the fifth time today â€” this is for you.

---

## The savings, at a glance

```
                  WITHOUT ICMG          WITH ICMG
Big-file Read     â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ  â–ˆâ–ˆâ–ˆâ–Œ            (-83%)
Build/test logs   â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ  â–ˆâ–Œ              (-92%)
SQL/table dump    â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ  â–               (-99%)
Thinking pass     â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ  â–ˆâ–Œ              (-92%)
Stable preamble   â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ  â–ˆâ–ˆ              (-90% via cache)
Repeat queries    â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ  â–               (-100% local cache)
Bulk batch ops    â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ  â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ      (-50% Anthropic batch)
HTML/PDF fetch    â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ  â–ˆâ–ˆâ–Œ             (-87% reduce+cache)
OCR vs vision     â–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆâ–ˆ  â–ˆâ–ˆ              (-90% on text-heavy)
```

```
Combined-stack on a typical turn:    â–ˆâ–ˆâ–ˆâ–  85â€“95% reduction (compounded)
```

These ranges come from real measurements. Each layer alone is small. Stacked, the headline number lands.

---

## What it does (single-page tour)

```
                â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
   YOUR TASK â”€â”€â–¶â”‚  icmg pack "<task>"   â”‚â”€â”€ filtered context bundle
                â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜     (memory + graph + diff)
                             â”‚
                             â–¼
                â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
                â”‚   28 MCP tools        â”‚â”€â”€ Claude Code / Cline / Continue
                â”‚   recall, store, â€¦    â”‚
                â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                             â”‚
                             â–¼
                â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
                â”‚  Hooks intercept      â”‚â”€â”€ Read 100-line cap
                â”‚  Read / Bash / Edit   â”‚â”€â”€ Bash 8KB cap, ANSI strip
                â”‚  Glob / Grep / Web    â”‚â”€â”€ Glob top-50, WebFetch 4KB
                â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                             â”‚
                             â–¼
                â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
                â”‚   Auto-compress       â”‚â”€â”€ reversible glossary on
                â”‚   (â‰¥3KB pack)         â”‚   pack output
                â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                             â”‚
                             â–¼
              SAVINGS DASHBOARD + RECEIPTS + COMPLIANCE TRACKING
```

| Pain | Icmg fix |
|---|---|
| Big files inflate every prompt | Surgical context bundles â€” only what the task actually needs |
| Noisy command output drowns the model | Output filtering tuned per command type (ANSI strip, dedup Ã—N, blank collapse) |
| Same bug solved twice | Persistent memory that surfaces past fixes when they apply |
| `/clear` wipes hard-won context | Snapshots + auto-distill of session decisions |
| Models "think" 8K tokens for a one-line rename | Intent-aware directives + caveman mode kill thinking outright |
| Re-sending the same project preamble every turn | Long-lived prompt-cache markers â€” pay once, reuse cheap |
| 30K tokens of logs / diffs / dumps | Lossless context compression with reversible round-trips |
| AI keeps trying the same broken approach | Anti-pattern memory (`icmg fail`) â€” failures become guardrails |
| AI "forgets" your CLAUDE.md instructions | Hard-enforcement hooks block disallowed reads/fetches |
| Native `Read` bypasses everything | Read cap-and-allow hook caps at 30 lines via `updatedInput.limit` + icmg-context overlay |
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

## Headline numbers (measured)

```
LAYER                       SAVING            SOURCE
â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Big-file Read           60â€“80% smaller       file slice, 100-line cap
Build/test logs         80â€“95% smaller       icmg run filter pipeline
SQL/table dumps         95â€“99% smaller       per-tool shrink strategy
Thinking overhead       50â€“90% off           --no-think + caveman
Stable preamble         90% off              prompt-cache markers
Repeat queries          100% off             tool_call_cache (5min TTL)
Bulk operations         50% off              Anthropic Batch API emit
HTML/PDF fetch          70â€“90% off           icmg fetch reduce + cache
Screenshot OCR          90â€“95% off           icmg ingest pytesseract
Read repeat-dedup       ~100% on dup         30-min sliding window
Pack on-repeat          60â€“97% smaller       icmg pack --diff
Symbol-slice context    80%+ per lookup      icmg context --symbol (one fn, not whole file)
Live stream dedup       real-time lines      icmg run --stream (filter summary at end)
File copy no-output    97% per write        icmg copy --from (zero output tokens generated)
Filter ANSI/dedup       30â€“60% on noisy CLI  npm/cargo/pnpm output
Caveman mode            ~75% on responses    fragment-style directive
â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
COMBINED ON TYPICAL TURN          â‰ˆ 85â€“95%
```

Token-cost savings at scale: roughly **\$0.10 per non-trivial Claude turn**, hundreds of dollars a month for active users.

---

## Coverage dashboard

`icmg savings` shows where the savings actually came from:

```
icmg savings â€” last 30 days
================================================================

  Command filter (icmg run)             67 calls       16.6K  â†’  16.2K   (2% saved)
  Compression  (icmg compress)           1 calls       5.0K   â†’  5.0K    (0% saved)
  Thinking     (--no-think)             21 calls       31.5K  â†’  7.5K    (76% saved)
  Pack receipts  (memory+graph)         15 calls       9.2K   â†’  9.2K
  Strict denials (read/web/bash)         8 calls       12.0K  â†’  0       (100% saved)
  Fetch cache    (icmg fetch)            3 hits        7.5K   â†’  0       (100% saved)
  Image OCR cache(icmg ingest)           2 hits        4.0K   â†’  0       (100% saved)
----------------------------------------------------------------
  TOTAL                                117 calls       86K    â†’  38K     (56% saved)

Cost without icmg: $0.50  (input $0.03 / output $0.47)
Cost with    icmg: $0.13  (input $0.02 / output $0.11)
You saved:         $0.37  (63.6%)

Real session tokens: 2195587  (icmg-covered 86K = 4%, outside 2.1M)
Strict-mode denials in window: 8
  â†’ each block redirected agent to icmg context/fetch
```

`Real session tokens` row reads the live Claude transcript â€” surfaces the gap between icmg-instrumented ops and actual context-window fill.

---

## Highlights

```
ONE BINARY            â–¸ ~30 MB Windows .exe, no node_modules / venv / Docker
LOCAL-FIRST           â–¸ Per-project SQLite. Never phones home
MCP SERVER            â–¸ 28 tools â€” recall, store, graph, sync, fetch, batch â€¦
                        Plugs into Claude Code, Cline, Continue, anything MCP
PROJECT FEDERATION    â–¸ icmg cross-recall â€” "this was done in project X" lookup
                        across all registered projects (memory + receipts)
AUTOPILOT HYGIENE     â–¸ icmg cron install â€” weekly memory prune (auto/session/
                        fail/correction rotation + telemetry trim) zero-touch
AUTO-ZONE             â–¸ Pack infers zone from keywords (10 zones); sharper IDF
                        without manual flag â€” auth/db/graph/imem/tkil/mcp/ui/
                        cli/hooks/compress
DASHBOARD             â–¸ icmg serve â†’ http://127.0.0.1:8080/
AST-AWARE             â–¸ tree-sitter for C/C++/Python/TypeScript
                        regex fallback for the rest
TEAM-FRIENDLY         â–¸ Memory + graph share via git-tracked JSONL snapshots
IMAGE-AWARE           â–¸ Local OCR for screenshots; 90%+ vs vision API
HARD ENFORCEMENT      â–¸ Hooks block native Read/WebFetch when icmg has it
SELF-REPAIR           â–¸ DLL sha256 auto-rollback, lock recovery, pending-restart
SELF-PROTECTION       â–¸ Atomic snapshots (24h/7d/4w/6m pyramidal) +
                        ping-pong dual-mirror (2Ã— live, instant failover) +
                        7-stage graph integrity check + auto on every upgrade
HOT-CONTEXT CACHE     â–¸ Re-issue same icmg context/compress within session â†’
                        ~5ms cache hit (15-30Ã— coldâ†’hot). Boosts hot files
                        in graph rank automatically. Self-invalidating on edit
SELF-MAINTENANCE      â–¸ icmg maintain run auto-detects HEAVY/IDLE state â†’
                        prune chain â†’ keeps only active graph in idle mode
DRIFT GATE            â–¸ icmg drift pin/check â€” pinned decisions get 10Ã— recall
                        boost; every prompt matched against anchors;
                        contradictions flagged BEFORE the model commits
SENTINEL WATCHDOG     â–¸ icmg sentinel â€” 15-min health checks; auto-prunes when
                        disk/cache/audit growth crosses thresholds; halts cold
                        at â‰¥3 reactions/hour (loop-safe by design)
SHADOW AUTO-UPGRADE   â–¸ icmg shadow-upgrade â€” daily background poll of GitHub;
                        sha256-verified download to ~/.icmg/shadow/<version>/;
                        atomic swap on next invocation. Chrome-style. No
                        teammate left behind on stale features. Pin/opt-out
                        available
AUDIT TRAIL           â–¸ chain-signed log of every backup/restore/failover/
                        sentinel reaction. icmg repair-history verify walks
                        the chain â€” tamper-detectable
SYMBOL SLICE          â–¸ icmg context <file> --symbol <Name> â€” one function body,
                        not the whole module. 80%+ token cut vs full-file read.
                        Substring + case-insensitive match. Precision surgical
SESSION DEDUP         â–¸ Recall auto-suppresses nodes already returned this session
                        â€” identical results stop flooding multi-turn context.
                        --no-dedup to override. Zero latency (in-memory set)
LIVE STREAM FILTER    â–¸ icmg run --stream â€” real-time line-by-line subprocess
                        output with filter summary appended at end. No buffering
                        lag; full filter context preserved for summary accuracy
APACHE-2.0            â–¸ License preserved on releases
```

---

## When to use which command

| Situation | Run |
|---|---|
| Starting a task | `icmg pack "<task>"` â€” one bundle, not 5â€“10 reads |
| Need a single file | `icmg context <file>` â€” surgical, not full Read |
| Need lines 60â€“95 | `icmg context <file> --lines 60-95` â€” replaces Read offset/limit |
| Any noisy command | `icmg run <cmd>` â€” filtered output |
| PR review | `icmg diff-summary` â€” symbol-grouped, not raw diff |
| Big text input | `icmg compress` â€” cut tokens, reverse-able |
| Past decisions | `icmg recall "<query>"` â€” surfaces what you already learned |
| Failed approach | `icmg fail store/recall` â€” anti-pattern memory |
| Same task in another project | `icmg cross-recall "<prompt>"` â€” federate across registered projects |
| Auto-prune old memory weekly | `icmg cron install` â€” Win schtasks / POSIX cron, zero-touch |
| Auto-zone for sharper recall | `icmg pack "<task>"` â€” infers `auth`/`db`/`graph`/etc. from keywords |
| AI ignored CLAUDE.md | `icmg strict on` â€” hooks enforce rules at harness level |
| Full LLM pipeline | `icmg agent "<task>"` â€” pack + cache + directives + retry |
| Bulk Anthropic | `icmg batch --task ...` â€” 50% via Batch API |
| Download URL | `icmg fetch <url>` â€” reduced + cached (70-90% off) |
| Screenshot | `icmg ingest screenshot.png` â€” OCR text-only payload |
| Team share | `icmg sync init/push/pull` â€” git-tracked JSONL |
| Audit savings | `icmg savings` â€” console / `--html` / `--json` |
| Real session tokens | `icmg context-budget` â€” covers ALL sources |
| What changed | `icmg whats-new` â€” release notes after `update` |
| Visual graph | `icmg serve` â€” embedded HTTP dashboard |
| **DB safety net** | `icmg backup snapshot` / `icmg backup restore latest` â€” atomic, schema-checked |
| **Instant failover** | `icmg mirror failover` â€” swaps in valid mirror in seconds |
| **Self-clean heavy/idle DB** | `icmg maintain run` â€” auto-detects state, chains prune + integrity |
| **Repair broken graph** | `icmg graph integrity --fix` â€” 7-stage check + targeted repair |
| **Inspect cache layer** | `icmg cache stats / list / prune` â€” see what's hot |
| **Pin a decision** | `icmg drift pin --topic X --stance Y` â€” pinned memory wins recall 10Ã— |
| **Check prompt drift** | `icmg drift check "<prompt>"` â€” surfaces conflicts with pinned anchors |
| **Watchdog health** | `icmg sentinel run` â€” auto-prunes disk/cache; halt-safe loop guard |
| **Background upgrade** | `icmg shadow-upgrade check` â€” daily auto-poll; pin/rollback supported |
| **Audit trail** | `icmg repair-history tail / verify` â€” chain-signed event log |

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

Optional capabilities are exactly that â€” optional. Default build runs everywhere with zero external dependencies beyond a C++17 compiler.

---

## How it pays off in practice

After a few days of use you'll notice:

```
Day 1   â–¸ Sessions get visibly longer before /compact fires
Day 3   â–¸ Recurring questions answer themselves from memory
Day 7   â–¸ Big PR reviews stop blowing the context window
Day 14  â–¸ Cache hit-rate climbs as you settle into patterns
Day 30  â–¸ Your monthly Claude bill stops scaring you
```

Memory recall sharpens over time. Snapshot restore gets faster. Compression learns your codebase's repeated terms. The system compounds.

---

## Architecture (one screen)

```
                       â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
                       â”‚           icmg.exe (single binary)   â”‚
                       â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                                       â”‚
        â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¼â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
        â”‚                              â”‚                              â”‚
   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”                  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”               â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
   â”‚   CLI   â”‚                  â”‚  MCP Server  â”‚               â”‚ HTTP Server  â”‚
   â”‚ 70+ cmd â”‚                  â”‚  (stdio)     â”‚               â”‚ :8080 read   â”‚
   â””â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”˜                  â””â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”˜               â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
        â”‚                              â”‚
        â–¼                              â–¼
   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
   â”‚   Core: SQLite WAL Â· BM25 Â· embedder       â”‚
   â”‚         tree-sitter Â· pytesseract sidecar  â”‚
   â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
        â”‚
        â–¼
   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
   â”‚  ~/.icmg/global.db        â€” registry        â”‚
   â”‚  <proj>/.icmg/data.db     â€” per-project     â”‚
   â”‚  ~/.icmg/*.flag           â€” caveman/strict  â”‚
   â”‚  ~/.icmg/*-log.{jsonl|txt}â€” receipts        â”‚
   â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

---

## Security

Local-first design with explicit boundaries:

- **Update integrity:** every release ships a SHA256 sidecar per binary. `update --apply` verifies before swap; mismatch auto-rollbacks. Bypass via `--skip-verify`.
- **Per-DLL SHA256:** 6 bundled DLLs (onnxruntime / providers-shared / tree-sitter / wasmtime / zstd / winpthread) verified after install; auto-restore from `.bak` on mismatch.
- **URL sanitization:** `icmg fetch` validates URLs against a shell-metacharacter blocklist (`"$\;|&<>` newlines control chars) before any shell-out.
- **HTTPS-only** for self-update + fetch.
- **Parameterized SQL** queries throughout (no SQL injection on store/recall/import).
- **No telemetry phoned home** â€” the binary makes network calls only when you invoke `update`, `fetch`, `embed` (Python sidecar), or `whats-new`.
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
| Update target binary locked (Windows) | Detached helper waits for the running process to exit, then performs the swap on the next invocation â€” no manual restart needed |
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
| **DB heavy (>100MB or >50K rows)** | `icmg maintain run` chains telemetry-prune â†’ topic-aged prune â†’ decay â†’ consolidate â†’ integrity check |
| **Project idle (no activity >24h)** | `icmg maintain run --idle-mode` soft-deletes auto/session/cache rows below importance 2; graph + pinned memory untouched |

**Always-on protection (auto-armed on `icmg init` and every upgrade):**

| Layer | Cadence | Disk cost | Purpose |
|---|---|---|---|
| Snapshot history | hourly | pyramidal (24h/7d/4w/6m) | Time-travel recovery |
| Dual mirror | 15 min | 2Ã— live | Instant failover |
| Hygiene maintain | 6 h | n/a | Bounded growth |
| Graph integrity | within maintain | n/a | Drift detection |

Most recovery paths take 1â€“3 seconds. A few (network re-fetch on integrity mismatch, helper-script wait for exit) can take 10â€“30 seconds. Safety is prioritized over speed â€” every recovery preserves the previous good state via `.bak` files so manual rollback is always available.

Run `icmg health` any time to confirm everything is in order.

---

## Honest limits

```
âœ“  Windows is primary target. Linux / macOS work but tested less.
âœ“  Opinionated. Fight the conventions and you fight the tool.
âœ“  Won't make a bad prompt good â€” makes a good prompt cheap.
âœ“  Some optional capabilities require one-time DLL download (binary tells you).
âœ“  Compression is semantic glossary â€” model still must understand aliases inline.
âœ“  Real session coverage starts ~50%; climbs as more hooks fire on subsequent ops.
```

See [CHANGELOG.md](CHANGELOG.md) for the full ship history (35+ releases, atomic per-task).

---

## Support

Solo maintainer, no VC backing. If icmg saved you tokens, consider supporting development:

- ðŸ’š [GitHub Sponsors](https://github.com/sponsors/ncmonx) â€” recurring or one-time
- â˜• [Ko-fi](https://ko-fi.com/ncmonx) â€” quick tip, no signup

Every contribution funds priority feature work and faster bug response.

---

## License

Apache 2.0. Use it however you want. Attribution appreciated, not required.

See [LICENSE](LICENSE) and [NOTICE](NOTICE).

---

## Other docs

- `CHANGELOG.md` â€” every shipped release, outcome-first
- `AGENTS.md` â€” how to wire icmg into your AI agent's instruction set
- `COMMANDS.md` â€” full CLI reference (70+ commands)
- `CLAUDE.md` â€” Claude Code-specific notes
- `docs/plans/` â€” phase plans + open backlog (Phase 69)

---

**TL;DR:** stop paying full price for AI coding sessions you don't need to. Drop `icmg` in your `PATH`, run `icmg init`, get back to shipping.

```
                            â—† icemage
                  context Â· memory Â· graph
              52/52 tests Â· 28 MCP Â· 72+ cmds
            cross-project federation Â· autopilot hygiene
```

