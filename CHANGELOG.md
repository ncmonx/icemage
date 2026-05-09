# Changelog

Token-saving CLI for AI coding sessions. Apache 2.0.

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
