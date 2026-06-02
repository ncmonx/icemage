# Changelog

All notable changes per release. Latest 5 detailed below; older versions: see
[GitHub Releases](https://github.com/ncmonx/icemage/releases). Each release ships
Linux + macOS (CI-built) and Windows binaries with SHA256 sidecars.

## v1.100.0

**FTS5 search snapshot: code/graph search is an indexed MATCH, not a full-table scan.** `icmg graph search` / `icmg_code_search` previously ran an O(n) `LIKE '%q%'` scan over every graph node. v1.100 adds a `graph_fts` FTS5 index (migration 0039, external-content over `graph_nodes`, trigger-synced) and routes search through `MATCH` + `bm25()`, falling back to `LIKE` if the index is absent (old DB / FTS5 not compiled). Query input becomes safe prefix terms (injection-proof). ~220x faster on a 19K-node graph (full-scan -> sub-millisecond). Full automated suite passes (1331 checks).

## v1.91.0

**Token-Efficiency v2: confidence-gated summaries, salience compression, and runtime dependency edges.** Three opt-in, local-first additions answering an external token-trimming critique: (1) **confidence-gated summarization** — `icmg gist` now scores each LLM summary (identifier-retention x length-sanity) and falls back to the deterministic heuristic when the summary is over-trimmed, off-topic, or hallucinated, so a lossy summary never reaches the main model; (2) **salience compression backend** — `icmg shrink --kind salience` keeps the most information-dense lines within a byte budget (pluggable scorer: heuristic now, llama-logprob perplexity later) as an LLMLingua-style alternative to the rule-based Tkil filters; (3) **runtime dependency edges** — `icmg graph runtime <trace>` parses python/node/gdb stack traces into `runtime_call` graph edges (dynamic execution flow, distinct from static `calls`). All additive; rule-based defaults unchanged. Full automated suite passes (1296 checks, +18).

## v1.90.0

**Security: the database key is now strictly hex-validated, and CodeQL stops scanning vendored code.** A CodeQL scan flagged two `cpp/sql-injection` paths where the SQLCipher encryption key (from an env var or key file) is interpolated into a `PRAGMA key=x'…'` blob literal — and `PRAGMA` cannot use bound parameters. v1.90 adds a fail-closed `isHexKey` gate inside `resolveDbKey`: any key that isn't strict hex (e.g. one containing a quote) is rejected and resolves to no key, so a crafted value can't escape the literal. It also adds a CodeQL config that path-ignores `third_party/` (vendored llama.cpp / sqlcipher), so scanning focuses on first-party code and ~159 upstream-owned findings are cleared. Full automated suite passes (1278 checks, +5).

## v1.89.0

**Graph prune, zone-scoped memory consolidation, multimodal graph nodes, and multi-agent work leases.** Four additions: (1) `icmg graph prune [--dry-run]` removes graph nodes whose backing file no longer exists, clearing scan-pollution (dead temp/build artifacts) that bloats the default zone; (2) `icmg memory consolidate --zone <name>` scopes near-duplicate collapse to a single zone, and the over-count hint now points to the correct command; (3) ingested media (`icmg ingest` image/PDF OCR) is recorded as a first-class graph node (`kind=multimodal`) so `icmg context`, graph queries, and zones surface it like source files; (4) `icmg session claim --scope <s>` / `leases` / `release` give conflict-free work leases across concurrent agents sharing one DB (stable identity via `ICMG_AGENT_ID`, stale leases auto-reclaimable). Full automated suite passes (1273 checks, +17).

## v1.88.0

**First CI-built release: Linux and macOS binaries are built and published automatically by GitHub Actions.** Pushing a version tag triggers a multi-platform matrix that compiles the full-feature build (ONNX + tree-sitter + llama/Vulkan + SQLCipher) on Linux and macOS, packages `tar.gz` + SHA256, and publishes the GitHub release — no manual cross-compilation. Windows is built locally (MSVC + sccache) and uploaded alongside. Adds release-automation scripts: `bump-version.ps1` syncs the version across all three source-of-truth files (`version.hpp` + `icmg.rc` + `CMakeLists`), and `release-win.ps1` refuses to upload unless the built binary's `--version` matches the tag. Full automated suite passes (1256 checks).

## v1.87.0

**Fix: `icmg update` and `icmg fetch` no longer fail on plain PowerShell**. Since v1.81, running icmg from a normal PowerShell prompt (not MSYS/Git-Bash) broke every command that calls the GitHub API or fetches a URL — `icmg update --check/--apply`, `icmg fetch`, the self-upgrade check — all returned "failed to query github (network or rate-limit)" even on a healthy connection. Two compounding causes: (1) in PowerShell, `curl` is an alias for `Invoke-WebRequest`, not the real curl, so its output was never the raw response body; (2) the internal shell wrapper passed commands to `pwsh -Command "..."` without escaping inner double-quotes, so a `-H "User-Agent: ..."` header silently broke argument parsing. v1.87 routes through `curl.exe` explicitly and escapes quotes in the wrapper — fixing all HTTP/JSON callers at once. Full automated suite passes (1256 checks).
