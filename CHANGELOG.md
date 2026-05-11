# Changelog

Token-saving CLI for AI coding sessions. Apache 2.0.

## 0.37.1 — directory traversal hardening

Graph scanning now skips symbolic links and filesystem junctions entirely. Previously, a directory junction pointing back toward a parent could cause the scanner to recurse without bound — freezing `graph scan` and `graph update` on affected paths. The fix is a single guard applied before any directory is entered: if the entry resolves as a link rather than a true directory, traversal stops there.

- **Symlink + junction guard** — `graph scan` and `graph update` no longer hang on paths containing symbolic links or NTFS junctions. Traversal skips them cleanly; all real directories and files continue to index normally.
- 50/50 ctest.

## 0.37.0 — fast path expansion + daemon scaffold

Read-tool event joins the in-process fast path. Cross-project surfacing and file-context hinting move into the consolidated handler. Persistent-server scaffold lands to reserve architecture for the next round of cold-start elimination.

- **Read-event in-process handler** — the pre-read hook now bypasses its multi-fork shell chain entirely. Same overlay, single process, sub-300 ms typical.
- **Cross-project memory surfacing** — when local store offers few matches, registered sibling projects are queried automatically; results tagged with their project of origin.
- **Path-mention awareness** — prompts that reference a tracked code path get a small graph-context snippet inlined alongside memory hits.
- **Daemon scaffold** — new `icmg daemon` surface (start/stop/status/restart) reserves the persistent-server architecture. Background IPC listener arrives in the next iteration; clients keep falling back to per-invocation mode automatically until then.
- **Read overlay env knobs** — `ICMG_NO_READ_HOOK`, `ICMG_NO_READ_FORCE`, `ICMG_READ_LIMIT`, `ICMG_SHRINK_THRESHOLD` all honored end-to-end.
- 50/50 ctest.

Combined effect with 0.36.0: prompt-submit + read-event hooks both fire fully in-process. Two of the highest-frequency event paths now share zero shell overhead.

## 0.36.0 — fast path: in-process prompt hook

The user-typed-prompt event now skips the shell-and-fork relay entirely. A single in-process handler reads stdin, consults the local store, and writes back rich context — replacing what used to be a chain of separate process spawns. Latency drops noticeably on every keypress that hits Submit.

- **In-process handler** for the prompt-submit event. Drift gate, memory surfacing, and large-paste hinting now resolve in one shot. No bash sub-shells, no jq forks, no per-call cold-start tax.
- **Early-exit guards** at the very top of the hook script — if the feature is disabled or icmg isn't on PATH, the hook returns in under a millisecond.
- **Lazy pinned-decision check** — when zero anchors are pinned, the drift logic short-circuits before any scan starts.
- **JSON output unchanged** — same `hookSpecificOutput.additionalContext` payload the IDE already consumes. Drop-in replacement; existing installs upgrade transparently after `icmg init`.
- 50/50 ctest.

Measured: prompt-submit hook fires in ~360 ms end-to-end (was ~600–1500 ms via the prior multi-fork chain). Token output identical; only the local pre-roll got faster.

## 0.35.1 — auto-on bulletproof patch

Patch release. Scheduler installation across **all five auto-on subsystems** now survives shell quoting, path-with-spaces, and elevation gating in one pass. Run `icmg <subsystem> auto-on` once — get a registered task or get a clear actionable instruction. No more silent failures, no more cryptic empty errors.

- **Single-arg scheduler wrappers.** Each auto-on writes a small wrapper file inside the project's hidden directory. The scheduler receives a single path argument — no nested quote escaping, no environmental shell quirks. Path-with-spaces just works.
- **Self-elevation flow.** When the OS refuses without elevation, icmg now triggers an elevation prompt automatically. Accept once → task registered. Decline → clean fallback printing the exact manual command you can run yourself.
- **Right schedule type for any interval.** Sub-hour, hour-aligned, and multi-day intervals each route to the correct underlying schedule kind. `24h` no longer means an invalid `HOURLY /MO 24` — it becomes proper daily scheduling.
- **Real error surfacing.** Failed operations now print the actual upstream message instead of empty diagnostics. Elevation-needed errors include a one-line hint pointing at the resolution.
- **Silent-success bug squashed.** One subsystem was reporting "installed" even on schedule failure. Now every auto-on path verifies before claiming success.
- 50/50 ctest.

Outcome: a teammate who upgrades on a different machine runs `icmg init` once, approves an elevation prompt once, gets the same protection profile as everyone else on the project — predictably.

## 0.35.0 — autonomy stack: drift gate + sentinel watchdog + shadow auto-upgrade

The autonomous-tool turn. icmg now defends its own state, watches its own footprint, and upgrades itself in the background — all without asking. Every layer ships with a kill-switch; defaults are sane.

- **Decision anchors (`icmg drift`).** Pin stances. `pin` records a decision; `check` matches every incoming prompt against pinned anchors and flags contradictions before the model commits. `supersede` is the *only* way to override — recency never silently wins. Pinned memory now gets a **10× recall boost** in BM25 — your principles surface above noise, every time.
- **Sentinel watchdog (`icmg sentinel`).** A loop-safe health monitor that runs every 15 minutes by default. Watches disk usage, snapshot freshness, mirror lag, cache bloat, audit-log growth, schedule presence, and live integrity. Auto-reacts when thresholds breach — prunes backups, refreshes mirrors, rotates audit logs — but **halts cold** the moment the repair counter sees ≥3 reactions/hour. Cannot loop. Cannot bloat.
- **Shadow auto-upgrade (`icmg shadow-upgrade`).** Chrome/VS Code-style background updates. Daily check polls GitHub releases; newer version found → downloads to `~/.icmg/shadow/<version>/` with mandatory sha256 verification → marks pending. Next invocation atomically swaps. Pin to a version, opt out entirely, or roll back in one command. No team-mate left behind on stale features.
- **Loop-safe repair (`icmg repair-history`).** Every backup-restore, mirror-failover, or sentinel auto-react now passes through a per-hour limiter (Phase 75 `RepairCounter`) and writes an entry to a chain-signed audit log. `repair-history verify` walks the chain and flags tamper. `repair-history count` shows the live rate. `repair-history reset-counter` after manual investigation.
- **7-stage graph integrity → 9 stages.** Added `embedding-drift` (detects + drops embeddings whose source file_hash changed) and `edge-type-vocab` (13-token whitelist with auto-canonicalize for common typos: `import`→`imports`, `invoked-by`→`calls`, etc.).
- **Hot-context-cache: per-agent namespacing.** `ICMG_AGENT_ID` env var partitions the tool-call cache. Multi-agent setups (Claude + Cursor + Cline on same project) no longer cross-poison each other's caches.
- **Auto-arm on every upgrade.** `icmg init` now arms 5 schedules by default — backup hourly, mirror 15-min, maintain 6-hour, sentinel 15-min, shadow-upgrade daily. Opt-out flags exist for each (`--no-backup`, `--no-mirror`, `--no-maintain`, `--no-sentinel`, `--no-auto-upgrade`).
- **PreCompact rule re-injection.** AGENTS.md ABSOLUTE RULE + top 5 pinned anchors emit back as `additionalContext` after every compaction. Your principles survive `/clear`.
- 50/50 ctest. 28 MCP tools. Migration schema bumped to v22 (`decisions` table + `memory_nodes.pinned` + `tool_call_cache.agent_id` + FTS5 virtual table staged).

**Stability impact:** 7 silent-failure modes converted to surfaced/halted/recoverable. Loop guards bound every repair cascade. Sentinel converts "DB grew to 8GB" surprises into 15-minute auto-reactions.

**Token impact:** drift-check at every prompt costs ~50-200ms but blocks 3-5K-token stance-flip turns. Pinned 10× boost means principles surface ahead of recency-only ranking. Combined with v0.34's hot-cache (~5ms re-issue) → multi-thousand-token saves per active session, compounding across the week.

More layers in progress under the hood — focus on memory operators, multi-host posture, and hands-free recovery. Concrete designs not published.

## 0.34.0 — self-protection: snapshot + dual-mirror + integrity gate + hot-cache

Fortress mode. Six-layer durability + speed stack flips on automatically the moment you upgrade. Zero touch, zero excuses for losing work or burning recompute on the same prompt twice.

- **Atomic snapshots** — `icmg backup snapshot` uses native backup API (WAL-safe, never blocks writes). Pyramidal retention (24h / 7d / 4w / 6m) caps disk while preserving history depth. Per-snapshot integrity sidecars detect bit-rot.
- **Schema-aware restore** — `icmg backup restore latest` rolls the world back to a known-good point. Auto-undo snapshot taken first, schema-version mismatch refused unless forced. Recovery point: minutes.
- **Ping-pong dual-mirror** — `icmg mirror sync` keeps two hot replicas ready for failover. Disk cost bounded to **2× live** — predictable. `icmg mirror failover` swaps in newest valid mirror in seconds, quarantines the corrupt primary for forensics, audit-logs the swap.
- **Self-maintenance** — `icmg maintain run` auto-detects HEAVY (DB > 100MB or > 50K rows) and IDLE (no activity > 24h), then chains: telemetry prune → topic-aged prune → decay → consolidate → graph integrity check. Idle-mode soft-deletes only `auto:%/session:%/cache:%` rows below importance 2 — graph nodes and pinned memory untouched. Fully recoverable from any snapshot.
- **Staged graph integrity** — `icmg graph integrity` walks 7 stages (schema, orphan edges, dead files, orphan symbols, stale mtime, duplicate paths, empty zones). Read-only by default, `--fix` repairs precisely (re-points edges via `UPDATE OR IGNORE`, marks stale for rescan, never destroys without warning).
- **Hot-context cache** — `icmg compress` and `icmg context` now keyed on file mtime + size; second invocation in same task returns cached output in **~5ms** (15-30× faster cold→hot). Boosts `graph_nodes.access_count` on every hit so hot files climb search ranking. Cache invalidates automatically when files change. `icmg cache stats/list/prune/clear` exposes the layer.
- **Auto-activate on every upgrade** — `icmg update --apply` post-swap calls `icmg init` which now auto-fires `backup snapshot` + `backup auto-on 1h` + `mirror auto-on 15m` + `maintain auto-on 6h`. First snapshot armed before user types another command. Opt-out flags (`--no-backup` / `--no-mirror` / `--no-maintain`) exist; defaults are sane.
- **Doctor + health rewired** — integrity FAIL now suggests mirror failover (instant) before backup restore (history). Health surfaces snapshot age, mirror count, heavy/idle flags so drift is visible at a glance.
- 50/50 ctest. 28 MCP tools.

**Token impact:** ~300-700K saved per active week from cache hits + sharper BM25 + priority boost. Catastrophic save: corrupted DB → mirror failover replaces a 30K-100K-token re-scan with seconds of downtime.

## 0.33.6 — cross-project federation + autopilot hygiene + auto-zone

- **Cross-project recall.** `icmg cross-recall <prompt>` searches all registered projects for tasks already solved. Each hit tagged `[project-name]`. UserPromptSubmit hook auto-falls-back when local <2 hits.
- **Memory hygiene autopilot.** `icmg cron install` schedules weekly Sun 03:00 task (Windows schtasks / POSIX cron) — bundles 5 prune cmds (auto:/session:/fail:/correction: rotation + telemetry trim). Zero-touch DB hygiene.
- **Auto-zone intelligence.** Pack now infers zone from task keywords across 10 zones (auth/db/graph/imem/tkil/mcp/ui/cli/hooks/compress). Sharper BM25 IDF without manual flag. `--no-auto-zone` opts out.
- **Batch tests +5.** Schema shape contract, 50-task scaling, special-char escape, 20K body pass-through, directive ordering. 14 total.
- 50/50 ctest. 30 MCP tools.

## 0.33.5 — force memory/compress/store/graph active

- **UserPromptSubmit hook** auto-injects top-3 memory hits + icmg-context for path mentions + compress suggestion on every prompt. 4 core features always-on.
- Read cap 100→30 lines + always injects rich icmg-context overlay (graph + symbols + memory). Edit-anchor sufficient.
- Pack auto-compress threshold 3KB→1KB.
- Distill min-len 200→100. Stop hook catches more decisions.

## 0.33.4 — coverage extensions

- Read deny→cap-and-allow (Anthropic Edit Read-first session req met + 80–90% saved)
- Pack auto-compress on outputs ≥3KB (telemetry source `compress-auto`)
- `icmg context` auto-scan-on-miss for TS/MD/JSON/YAML/TOML/XML/SH/PS1
- Read repeat-dedup 30min sliding window → 10-line cap
- Bash output cap 50KB→8KB
- PostToolUse:Bash\|PowerShell, Glob\|Grep top-50, WebFetch 4KB caps
- SessionStart token-spent warning when real session >50K

## 0.33.3 — auto-scan + 7-layer dashboard + agent retry + 21 unit tests

- Savings dashboard expanded 3→7 layers (pack receipts, strict denials, fetch cache, image OCR cache)
- `icmg agent` retries 429/503/504 + rate-limit substrings, exp backoff 2/4/8s
- CHANGELOG.md consolidated
- 3 new test files (filter passes, ref registry, pack delta) — 21 cases

## 0.33.2 — caveman thinking enforcement + dashboard real-total

- Hard fix: `pack` auto-couples no-think+caveman when `~/.icmg/caveman.flag` set
- Stop hook logs thinking-phase violations to `~/.icmg/compliance-violations.jsonl`
- SessionStart prepends NOTE / REMINDER / STRONG WARNING based on 24h count
- Savings dashboard shows real session tokens + icmg coverage % + outside coverage
- 8 new MCP wrappers (fail/distill/correction/receipt/entropy/tool-budget/shorten/context-budget) — total 28 MCP tools
- `icmg mcp list` discoverability
- MCP-layer `tool_call_cache` for read-only tools (5min TTL)
- Memory age-decay envelope (90d half-life)
- `icmg memory prune-old --topic <prefix> --older Nd` rotates auto-grown topics
- 47/47 ctest

## 0.33.1 — filter pipeline + context-budget + 7 features

- Default filter: ANSI strip + dedup × N + blank-collapse + CR-overwrite drop (50% smoke)
- `icmg context-budget` parses Claude transcript JSONL → real token usage by source (135× discrepancy revealed)
- `pack --budget N` knapsack hits, `pack --siblings`, `context --lines A-B`
- `receipt by-file --top N`, `savings` input/output cost split
- `icmg shorten` (47% smoke), `icmg tool-budget` per-turn gate
- 47/47 ctest

## 0.33.0 — Phase 67 batch (9 features + bug fix)

- `icmg fail` anti-pattern memory + `icmg correction` hook capture
- `icmg distill` Stop + PreCompact hooks auto-extract decisions/sessions
- `icmg receipt` per-pack token cost ledger (migration 0020)
- `icmg entropy` git-log file-edit heatmap
- `pack --ref-ids` per-session [ICMG-MEM-N] anchors with reuse
- `pack --prune-audit`, `pack --auto-cache` (4KB threshold)
- BUG FIX: `icmg context` emits file body excerpt by default
- 47/47 ctest

## 0.32.10 — differential pack

- `icmg pack --diff` emits delta vs last pack (97% smoke)
- `--diff-reset` clears baseline

## 0.32.9 — anti-pattern + drift detector + diff-aware tests

- `icmg fail store/recall/list`
- `icmg lint-claudemd --strict` CI gate
- `icmg test-select --runner pytest|vitest|jest|cargo|dotnet`

## 0.32.8 — discover + diff-summary perf

- `discover` mtime-filter transcripts BEFORE parse, `--max-files 200` cap
- `diff-summary` single-shot `git diff --unified=0` (10-100× faster)

## 0.32.7 — SP normalize + findSymbol guards

- `sp impact-table` accepts `[DB].[dbo].[Order]` / `DB.dbo.Order` 3-part names
- `findSymbol` skips substring + JSON fallbacks for queries < 3 chars

## 0.32.6 — auto-think default + telemetry hygiene

- `icmg pack` defaults to auto-think (intent classifier + --no-think on simple)
- `--full-think` opt-out
- Telemetry only writes when directive fires (no 0%-saved noise rows)

## 0.32.5 — JS/TS symbol JSON-array + scan opt-out

- `findSymbol` 5th fallback: `symbols` JSON column with token-boundary patterns
- `graph scan --no-mem-sync` (alias `--no-embed`) skips ALL-nodes post-pass

## 0.32.4 — graph search + symbol resolution + fast incremental

- `graph search` covers `symbol_name` + `signature` with relevance ordering
- `findSymbol` 3-layer fallback (case-ins → suffix → substring)
- `graph update <file> --no-mem-sync` (10min+ → seconds)
- Db auto-migrate scoped to `user_version > 0`

## 0.32.3 — path normalization + auto-migrate

- Curl wrappers normalize Windows tmp paths backslash → forward slash
- Db ctor auto-runs pending embedded migrations when stuck pre-migration

## 0.32.2 — strict-mode telemetry + savings integration

- Hooks log denials to `~/.icmg/strict-denials.jsonl` (read-strict / bash-strict / webfetch-strict)
- `icmg savings` aggregates by hook within window

## 0.32.1 — `icmg doctor`

- 6-category check + auto-repair (missing hooks, orphan .bak/.pending-restart, stale caveman, missing DLLs, DB integrity, strict-flag drift)

## 0.32.0 — strict mode WebFetch/Bash + DLL auto-rollback

- `icmg strict` deny WebFetch + Bash cat/head/tail >20KB
- DLL SHA256 mismatch on update → auto-restore from .bak

## 0.31.5 — MSYS routing fix

- Bare command names route through `bash -c` with `/usr/bin:/mingw64/bin` prepended
- Resolves `find`/`pnpm`/`.cmd shim` failures under Git Bash / MSYS2

## 0.31.4 — strict mode + Icemage brand mark

- `icmg strict on/off/status` toggles `~/.icmg/strict.flag`
- `icmg update --apply` propagates strict mode automatically
- Logo + favicon embedded across README, dashboards, Windows .exe

## 0.31.3 — MCP icmg_compress telemetry fix

- Wrapper now writes `GlossaryStore::recordTelemetry` on shared db handle

## 0.31.2 — batch tests + Self-repair docs

- `buildBatchSpec` extracted free fn + 9 unit tests
- README Self-repair section

## 0.31.1 — per-DLL SHA256 + HTML reduce tests

- `update --apply` post-install verifies 6 bundled DLLs against `.sha256` sidecars
- HTML reducer extracted + 8 unit tests

## 0.31.0 — health + telemetry prune + auto hook refresh

- `icmg health` single sanity check (8 categories)
- `memory prune-telemetry` 90d default
- `update --apply` post-swap auto hook refresh
- Caveman SessionStart hook + last-trigger timestamp

## 0.30.x — savings daily chart + caveman directive

- Savings `--html` per-day SVG bar chart
- `icmg caveman on/off/status/level` toggles `~/.icmg/caveman.flag`

## 0.29.x — sync + URL sanitize + 5 MCP tools + sha256 sidecars

- `icmg sync init/push/pull/merge/status` via JSONL snapshots in `.icmg/sync/`
- URL sanitization (shell metachar blocklist, http/https only)
- 5 new MCP tools (sync/ingest/fetch/batch/savings)

## Earlier

Phases 27-46 ship the core: ICM memory + BM25, KGraph (file + symbol nodes), Tkil command filter, AST extractors (C/C++/TS/Py via tree-sitter), embedding sidecar (sentence-transformers fallback to BM25-only), MCP server (28 tools), prompt cache markers, batch API emitter, image OCR via pytesseract sidecar, multi-project registry, abbreviation engine, stored-procedure store, visual graph (Cytoscape.js), self-update from GitHub releases.

See `git log` or per-tag release notes on https://github.com/ncmonx/icm-graph/releases for prior detail.
