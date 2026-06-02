# Changelog

All notable changes per release. Latest 5 detailed below; older versions: see
[GitHub Releases](https://github.com/ncmonx/icemage/releases). Each release ships
Linux + macOS (CI-built) and Windows binaries with SHA256 sidecars.

## v1.100.0

**FTS5 search snapshot: code/graph search is an indexed MATCH, not a full-table scan.** `icmg graph search` / `icmg_code_search` previously ran an O(n) `LIKE '%q%'` scan over every graph node. v1.100 adds a `graph_fts` FTS5 index (migration 0039, external-content over `graph_nodes`, trigger-synced) and routes search through `MATCH` + `bm25()`, falling back to `LIKE` if the index is absent (old DB / FTS5 not compiled). Query input becomes safe prefix terms (injection-proof). ~220x faster on a 19K-node graph (full-scan -> sub-millisecond). Full automated suite passes (1331 checks).

## v1.99.0

**Temporal knowledge graph + API-spec compilation.** Two token-savers: (1) `icmg graph recent` ranks files by recency-decayed centrality (exponential half-life decay on `updated_at`, blended with degree-centrality; `--halflife-days` / `--limit`), so onboarding focuses on what is hot, not what is merely big. (2) `icmg apispec <openapi.json>` compiles a verbose OpenAPI document into a dense endpoint map (`METHOD /path - summary (N params)` per line) instead of feeding the whole spec to the model. Both pure + deterministic. Full automated suite passes (1325 checks).

## v1.98.0

**Dynamic toolsets: expose only the MCP tools you need.** The MCP server ships 41 tools; sending all their schemas on every `tools/list` is wasteful when an agent only uses a handful. v1.98 lets you scope the exposed set: `ICMG_MCP_PROFILE=core` serves a curated ~10 essentials, or `ICMG_MCP_TOOLS=icmg_recall,icmg_code_search,...` an explicit allowlist (wins over profile). Unset = all tools (back-compat). Smaller tool-list payload, less context spent before the agent does anything. Full automated suite passes (1316 checks).

## v1.97.0

**The B:/ "drive not found" popup-killer now self-heals.** v1.92 added a background daemon to auto-dismiss the Windows hard-error dialog (modal, can hang a hook subprocess and freeze the agent). But it only started at SessionStart, so if killed mid-session it stayed dead. v1.97 makes both the SessionStart and per-prompt (UserPromptSubmit) hooks call `popup-killer ensure` - an idempotent, near-zero-cost check (named mutex) that relaunches the daemon before the next turn if it died. The drive-popup guard is now always live. Full automated suite passes (1311 checks).

## v1.96.0

**AI is icmg-compliant from turn one.** Previously, every fresh session an agent might `grep`/`Read` your `AGENTS.md`/`CLAUDE.md` (or ignore icmg) until reminded. v1.96 makes the SessionStart hook inject a concise standing-rules directive every session: project rules are already loaded as agent config (do not grep/read them), and every action should go through icmg first (`recall`, `context`, `code_search`, `run`, `parallel`), with the post-change sync reflexes. Installed automatically by `icmg init`/`--force`. Full automated suite passes (1311 checks).