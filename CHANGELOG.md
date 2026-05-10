# Changelog

Token-saving CLI for AI coding sessions. Apache 2.0.

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
