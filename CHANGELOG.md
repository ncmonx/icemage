# Changelog

All notable changes per release. Latest 5 detailed below; older versions: see
[GitHub Releases](https://github.com/ncmonx/icemage/releases). Each release ships
Linux + macOS (CI-built) and Windows binaries with SHA256 sidecars.

## v2.22.0

**Brain v2.22: the memory learns from its own usage.** Four deterministic
features from the 2026-08-25 research pass (Zep/Graphiti temporal-KG, Mem0,
MemoryOS, coarse-to-fine grounded memory), all zero-LLM:

- **Time-travel recall** (`recall --as-of T`) — point-in-time recall over the
  existing bi-temporal columns: what did we believe at T? Since-superseded
  facts reappear before their `invalidated_at`; facts not yet valid stay
  hidden. Accepts epoch, `7d`/`24h`/`30m` (that long ago), `YYYY-MM-DD` (UTC).
  Side-effect-free: no cache, no frequency bump.
- **Retrieval-failure ledger** (`memory-health --gaps`) — recall queries in
  the last `--days` (7) that returned ≤ `--max-results` (0) results are
  knowledge-gap signals: the agent asked, the brain had nothing. Strongest
  (most-asked) first, noise queries skipped, `--json` for tooling.
- **Quick-note promotion by heat** (`memory-consolidate --promote-quick`) —
  `quick:<epoch>` captures the agent kept recalling (`--min-freq`, default 3)
  get a permanent searchable topic (`hot:<kw1>-<kw2>-<kw3>`); cold ones keep
  aging out via decay. `--dry-run` / `--json`.
- **Coarse-to-fine recall view** — when a recall result set exceeds ~1200
  tokens, the strongest hits stay full-bodied and the tail collapses to
  1-line index rows (`recall --get <id>` fetches detail). `--full` opts out.

Fixed en route: SQLite CASE-expression affinity gotcha (`CAST(? AS INTEGER)`
required or a TEXT-bound param wins every comparison).

23 new tests. **2447/2447 ✓.**

## v2.21.1

**Multi-daemon spam fix.** On busy multi-user servers dozens of idle
`icmg.exe` processes accumulated per user. Root cause: concurrent hook calls
raced `ensureDaemon()`; every spawned rule-daemon created the *same* named
pipe successfully (`PIPE_UNLIMITED_INSTANCES`, no
`FILE_FLAG_FIRST_PIPE_INSTANCE`, no cross-process lock) and then parked in
`ConnectNamedPipe` forever. Fixed belt-and-braces:

- `RuleDaemon::acquireSingleton()` — per-user cross-process lock (Windows:
  named mutex `Local\icmg-rule-daemon-<user>`; POSIX: `flock` on
  `~/.icmg/rule-daemon.lock`). `run()` requires it; the losing daemon exits
  quietly.
- `CreateNamedPipeA` now passes `FILE_FLAG_FIRST_PIPE_INSTANCE`, so a raced
  second daemon can never share the pipe even if the mutex path regresses.

Cleanup on affected servers: `taskkill /F /IM icmg.exe` once, then update.

3 new tests (2419 total).

## v2.21.0

**Brain + token trio.** Three deterministic memory-quality upgrades, no LLM
required:

- **Session-aware recall delta** -- when the session TTL dedup suppresses
  memory nodes you already saw this session, `recall` now emits a single
  stdout line `[N prior memories still apply: #ids]` instead of silently
  dropping them (previously stderr-only). The agent keeps the reference
  without re-paying the tokens for full bodies.
- **Contradiction sentinel** (`memory-health --contradictions`) -- scans the
  memory store for node pairs that overlap heavily (Jaccard >= 0.6 default,
  `--jaccard-min` to tune) yet disagree: a negation marker on one side or a
  conflicting `key=value` fact. Flag-only, never deletes; each hit suggests
  the existing bi-temporal fix (`icmg memory invalidate <old> --by <new>`).
  Strongest-first ordering, capped at `--max` (default 25) so a 31k-node
  store stays readable. Verified live: caught real `icmg_version` and
  `prefix` conflicts at 100% overlap.
- **Adaptive recall depth** (`recall --adaptive`) -- recall limit follows the
  deterministic intent classifier from v2.20 effort-hint: simple task = 3
  results, unknown = 7, complex = 12. An explicit `--limit` always wins.

9 new tests (2416 total).

## v2.20.0

**Model-era capability pack (deep-research 2026-07-22).** As frontier models grew
1M-token windows, extended-thinking budgets, and multi-agent orchestration, the
token lever shifted from *how much* to trim toward *cache-hit rate* and
*reasoning-token* cost. Seven gaps researched; five shipped (two already covered
in-tree, not rebuilt):

- **Cache-aware context assembly** (`pack --cache-aware`) -- prompt caching
  (-90% cost / -85% latency, Anthropic) only pays off with a byte-stable cached
  prefix, but the old whole-blob wrap mutated on line 1 (per-task header) so the
  cache never hit. New `cache_layout` classifies sections (conventions / rules /
  graph / files = stable; task / recall / diff = volatile), orders stable-first,
  wraps ONLY the stable prefix, and reports an FNV-1a `prefix_hash` so drift is
  visible. Verified: two different tasks yield an identical prefix hash.
- **MCP tool annotations** -- every tools/list entry now carries
  `readOnlyHint` / `destructiveHint` / `idempotentHint` / `openWorldHint`,
  derived from each tool's `isMutating()` signal (fetch/ingest/sync are
  open-world). Lets an agent host plan safely (don't retry non-idempotent, warn
  before destructive). 41/41 tools annotated.
- **`icmg_graph_query` MCP tool** -- deterministic multi-hop structural search
  (`blast_radius` | `who_calls` | `path_between`): the cheap traversal a code
  model can't reconstruct from flat grep.
- **`pack --effort-hint`** -- deterministic extended-thinking budget
  recommendation from task intent + graph fan-out (`<icmg-effort>` directive;
  advisory, off by default).
- **`token-ledger stats` / `otel`** -- cache-hit ratio (the dominant 2026 cost
  lever) + honest cost estimate over the existing ledger; `otel` emits
  OpenTelemetry GenAI-style JSON offline.

All deterministic, no-LLM, local-first, TDD. Full suite 2406 tests. Anti-dup:
cross-session resume-brief already covered by `wakeup` + layer0
failed-validation extraction; multi-agent work-claim already shipped as
`agent_leases` + `session claim`.

## v2.19.1

**`icmg run` destructive-op guard: argv-aware, no more false positives.** The
guard that gates `rm -rf`/`Remove-Item`/`DROP TABLE`/etc. used a whole-string
substring scan, so any command that merely *contained* `rm ` + `-f`/`-r`
anywhere -- `grep 'rm -rf' notes.txt`, a path like `src/farm/`, a search pattern
-- was wrongly refused, and a `--yes`/`ICMG_ASSUME_YES=1` bypass set on an
auto-wrapped child command never reached the icmg process so it kept denying.
Now the detection is argv-aware (`isDestructiveArgv` in `run_args.hpp`): it flags
only when the *leading verb* is the destructive tool (skipping `env VAR=val` /
`sudo` prefixes and splitting a single quoted shell line), and a leading
`ICMG_ASSUME_YES=1` / `FORCE=1` env-prefix is honored as explicit bypass intent
(`hasInlineYesPrefix`). SQL statements passed as a db-CLI argument
(`psql -c "DROP TABLE t"`) are still caught; `git rm --cached` (non-destructive)
no longer trips. 10 new tests (`tests/cli/test_run_args.cpp`). TDD.

## v2.19.0

**graphify-parity ingest & graph pack.** Five gaps from the graphify landscape
research, closed: Office ingest (`.docx`/`.xlsx`), audio/video transcription
(faster-whisper), SQL schema graph (`.sql`/`.ddl`), HCL/Terraform graph
(`.tf`/`.hcl`), and one-shot multi-host wiring (`init --all-tools`/`--strict`).
34 new tests; 2364 C++ + 15 Python green.


- **`icmg ingest` now transcribes audio/video (`.mp4` `.mov` `.mkv` `.webm`
  `.mp3` `.wav` `.m4a` `.flac` …).** Closes gap G2 from the graphify research.
  Transcription via faster-whisper (sidecar `extract_media`; CPU int8 `base`
  model by default, overridable with `ICMG_WHISPER_MODEL/DEVICE/COMPUTE`).
  faster-whisper bundles its own audio decoding (PyAV), so ffmpeg is not
  required for common containers. Graceful when the dep is absent: a clear
  "install faster-whisper" message, never a crash. Media nodes are recorded as
  `video`/`audio` in the graph. TDD: sidecar tests skip the heavy run when
  faster-whisper is present and assert the dep-guard shape when it's absent.
- **HCL / Terraform extractor (`.tf` / `.hcl` / `.tfvars`).** Closes gap G4 from
  the graphify research. Top-level blocks (`resource`, `data`, `module`,
  `variable`, `output`, `provider`, `locals`, `terraform`, …) become graph nodes
  as `<type>.<labels>` (in `classes`), each block's local name in `functions`,
  and a `module` block's `source` becomes a `module:<source>` dependency edge
  (in `imports`). Comment-tolerant (`#`, `//`, `/* */`). New `HclExtractor`
  (`src/graph/extractor/hcl_extractor.{hpp,cpp}`), 8 tests. Scanner now maps
  `.tf`/`.hcl`/`.tfvars` → `hcl`. Verified E2E. TDD.
- **`icmg init --all-tools` / `--strict` — one-shot multi-host wiring.** Closes
  gap G6 (graphify's `install --strict` parity). Previously `init --tool <name>`
  handled one host CLI at a time and was hint-only for non-Claude-Code tools.
  Now `--all-tools` detects every supported host present in the project
  (Cursor, Windsurf, Zed, Codex, Copilot, OpenCode, Gemini, Amp) and drops a
  real icmg routing-rule file at each one's config path in a single run;
  `--strict` also wires hosts not yet detected (proactive). Claude Code still
  gets the full native hook setup. New pure, header-only `src/cli/tool_wiring.hpp`
  (`knownTools` / `routingContent` / `isToolPresent` / `writeRouting`), 6 tests
  (`tests/cli/test_tool_wiring.cpp`). Verified end-to-end. TDD.
- **SQL schema extractor (`.sql` / `.ddl`).** Closes gap G3 from the graphify
  research: `.sql` files now feed the code graph instead of only getting a
  generic regex scan. `CREATE TABLE` → table nodes, `CREATE VIEW/FUNCTION/
  PROCEDURE/TRIGGER` → routine nodes, `FOREIGN KEY … REFERENCES` / inline
  `REFERENCES` → `references:<table>` edges (table-to-table). Dialect-tolerant:
  `IF NOT EXISTS`, quoted / backticked / bracketed and schema-qualified names,
  line `--` and `/* */` comments stripped. New `SqlExtractor`
  (`src/graph/extractor/sql_extractor.{hpp,cpp}`), 10 tests
  (`tests/graph/test_sql_extractor.cpp`). Verified end-to-end: `icmg graph
  update` on a `.sql` file emits `[table]` / `[view]` nodes. TDD.
- **`icmg ingest` now reads Office documents (`.docx` / `.xlsx`).** Closes gap
  G1 from the graphify landscape research: graphify ingests office files, icemage
  previously only did PDF + image OCR. `.docx` extracts paragraphs + table cells;
  `.xlsx` extracts every sheet's cells (sheet-titled, tab-joined rows). Routed
  through the ingest sidecar (`multimodal/icmg_ingest.py` → `extract_office`,
  python-docx / openpyxl, graceful when a dep is missing). Office extraction is
  exact, so it skips the OCR confidence-gate / vision-recommendation path and is
  labelled `office` (not `image`) in the summary line. New standalone sidecar
  test suite `multimodal/test_ingest.py` (10 cases, wired into CTest as
  `test_ingest_sidecar`; skips office round-trips gracefully if the Python deps
  are absent). TDD.

## v2.18.0

**Filter-coverage telemetry goes proactive + a stray-`nul` daemon bug fix.** Five changes, all TDD:

- **`icmg savings` now self-diagnoses filter-coverage gaps.** It surfaces the command verbs burning the most raw output while Tkil saves the least — i.e. missing or weak filters. Born from two same-week incidents (`git log` 7-char hash regex miss, `gh api` 0% filtered) that were both found *reactively* only after a human eyeballed `token_ledger`. Now a coverage hole shows up in the dashboard the moment it costs tokens.
- **`icmg learn` turns those gaps into an actionable recommendation** — for each high-waste verb it prints the concrete next step (JSON-minify filter for `gh`/`curl`-style verbs, extend the matching filter for `git`/`docker` dispatchers, or a generic cap/summarise filter otherwise), pointing at the exact `src/tkil/filters/` pattern.
- **`icmg savings --json` now exposes a `filter_gaps` array** so the badge/CI/tooling can flag an uncovered high-waste verb without parsing prose.
- **Fix: stray `nul` file bug.** `RuleDaemonClient::ensureDaemon()` spawned the daemon via `safeExecShell()` with a cmd.exe-specific redirect string (`start /b icmg rule-daemon start >nul 2>nul`). `safeExecShell()` prefers bash whenever `bash.exe` is found — so on any Windows box with Git installed this ran under bash, where `start` is unknown (daemon never spawned) and `nul` is an ordinary filename, so `>nul` created a literal `nul` file in the current directory *on every `icmg` invocation*. Fixed with a plain-argv spawn (`daemonSpawnArgv()`) via `safeExec()` — no shell in the middle.
- **Audit: three more `safeExecShell` sites** (`schedule_helper`, `backup_cmd`, `service_cmd`) carried the same Windows-`nul`-through-bash hazard; all now route through a new bash-safe `core::suppressStderr()` helper (`2>/dev/null`).
- **`icmg doctor` now sweeps leftover `nul` files.** The spawn fixes above stop *new* stray `nul` files, but the ones already created (one per prior `icmg` invocation, scattered across the cwd, `~`, and `~/bin`) remained — and a bare `nul` on `PATH` (`~/bin/nul`) actively breaks tooling. Doctor now finds and removes them (reported under `[nul]`; `--dry-run` lists without deleting). Deletion uses the Win32 extended-length prefix (`\\?\` + `_wremove`) because a plain remove of a final `nul` component is intercepted by Windows' DOS-device mapping and silently targets the NUL *device*, leaving the real file on disk. New `src/core/nul_sweep.hpp` (`isStrayNulName` / `sweepStrayNulFiles` / `removeStrayNul`), 8 tests.
- **Build fix (CI hotfix on top of v2.18.0).** The v2.18.0 tag referenced `tests/core/test_nul_sweep.cpp` from `CMakeLists.txt` but the source (and its `nul_sweep.hpp` header) were never `git add`ed, so the CMake *generate* step failed on the Linux/macOS runners (`Cannot find source file` → `No SOURCES given to target: icmg_test`). Both files are now committed.

**2340/2340 tests ✓** (Windows; +2 POSIX-only remove-path tests on the Linux/macOS runners).

## v2.17.0

**`gh api` JSON output had zero Tkil filter coverage.** Found while investigating why a release/CI-heavy session's "Command filter" savings showed only ~17% (much lower than the 60-99% typical for git/grep/build commands) — `gh api <endpoint>` had no `CmdType` at all, so every call fell through to `CmdType::Default` and passed through raw. Production telemetry: a single `gh api gists/<id>` call emitted 36,741 raw bytes with `filtered_bytes` IDENTICAL to raw (0% saved). New `GhFilter` minifies `gh api`'s default pretty-printed JSON (2-space indent) when it round-trips as valid JSON — a strictly lossless transform (the parsed value is byte-for-byte identical before/after), typically cutting 30-50% of bytes. Non-JSON `gh` output (`gh pr view` tables, plain-text errors) and malformed/truncated JSON both fall through to raw passthrough unchanged — never invents a truncation that could hide real API data. New `CmdType::Gh` + detector patterns for `gh api/pr/issue/release/run/repo/gist/workflow`. TDD: 5 new tests first (`test_gh_filter.cpp`), all green. Verified live against a real `gh api` call. **2309/2309 tests ✓.**

## v2.16.0

**Fix `icmg savings` daily-history bug (user-reported).** The "Daily real-token history" block (console + `--html`) aggregated a DIFFERENT source than the headline "Real API tokens" number — it shelled out to `context-budget --all-sessions`, bucketing by transcript-file MTIME with a TEXT-LENGTH ESTIMATE, while the headline reads `token_ledger` directly. Symptom matched exactly: busy multi-day sessions vanished (only the file's last-mtime day got credited) and shown days were off by 2-1000x. Fixed with `aggregateTokenLedgerByDay()`, a pure function (`token_ledger.hpp`) that buckets `token_ledger` itself by each row's own local-calendar day — now both numbers always reconcile against the same source of truth. Wired into both the console daily list and the `--html` daily table (the HTML table's misleading "Sessions" column, which was actually counting transcript-derived buckets, is now correctly labeled "Turns"). TDD: 5 new tests written first (`test_token_ledger.cpp`), including a regression guard for the exact reported symptom — a busy day with many turns must never be dropped. **2304/2304 tests ✓.**

## v2.15.2

**Search-accuracy hardening part 2: 2 more root-cause fixes (found by continuing the v2.15.1 telemetry investigation), plus a stale-headline-numbers correction.** (1) After fixing the `bash -c` shell-wrapper misclassification in v2.15.1, a sibling project's savings-dashboard baseline still sat at only ~33-40%. Root cause: the icemage-code agent harness (a separate codebase) also wraps commands as `powershell -NoProfile -ExecutionPolicy Bypass -File <temp>\run.ps1`, writing the real command into a temp script file rather than the command string itself — `Detector::detect()` never unwrapped this shape either (3511 production invocations, 12.7MB raw, only ~38% filtered). Fixed with a best-effort read of the script's content at classification time (safe: `detect()` runs before execution, so the script is normally still on disk; degrades silently to `Default` if the file is gone). (2) Continuing the same investigation past the wrapper fixes (baseline still ~42%) found `GitFilter`'s commit-hash regex required an 8-40 char hex prefix, but `git log --oneline` — the overwhelmingly common default — prints Git's own 7-char short hash, so the regex never matched and the "cap at 30 entries" truncation was structurally unreachable (a single `git log --oneline` call emitted 76,551 raw bytes with 0% filtered). Fixed with a one-character regex change (`{8,40}` → `{7,40}`). Both `SearchFilter` and `GitFilter` had **zero prior test coverage** — `GitFilter` now has 3 new tests exercising it through the same `Registry<BaseFilter>` lookup path Tkil itself uses. Also: this release corrects a **long-standing stale headline number** unrelated to the above fixes — the README's CLI-commands badge said "95+" while `grep -c ICMG_REGISTER_COMMAND` shows **200** actually registered (drift predates this session; the GitHub repo "About" description was also updated, both are now part of the standing release docs-sync gate going forward). All TDD, deterministic, zero-LLM. **2298/2298 tests ✓.**

## v2.15.1

**Search-accuracy hardening: 5 root-cause fixes found via production telemetry, not speculative research.** (1) Hybrid recall's BM25 normalization used a rank-POSITION fallback (top candidate=1.0, decreasing by list index) instead of the real `bm25_score` magnitude already populated by `Scorer::rank()` — distorted the blend for both near-tied and wildly-different candidates. Fixed via a new pure `Scorer::normalizeMinMax()`. (2) **Entity-linking recall boost** — `extractEntities()` tagged memory `keywords` with url/ip/env/mention tokens at CAPTURE time (since 2026-06-07) but recall never cross-referenced the QUERY's own entities against them; a query naming the same concrete URL/env-var/@mention as a memory now gets a deterministic top-up via `Scorer::entityOverlapScore()`. (3) The documented `--fuzzy` CLI flag ("Fuzzy search fallback") was a literal no-op — the parameter was `bool /*fuzzy*/`, never read. Now wired to a real bounded-Levenshtein fallback (`Scorer::levenshteinCapped`/`fuzzyTokenOverlap`) that only fires when the exact BM25 pass returns empty. (4) Investigating a user-reported savings-dashboard anomaly (one project showing only ~1% average Tkil filtering vs ~99% in a sibling project) found every Tkil filter's truncation logic bounded output by LINE COUNT only, never byte size — a single pathological command (one giant line, no newlines) sailed through 100% unfiltered (472,581,003 raw bytes, 0% saved, confirmed in production telemetry). Fixed with a universal 2 MiB `capRawBytes()` gate inside `splitLines()`, the one function all 19 registered filters call first. (5) Same investigation found commands routed through a shell wrapper (`bash -c "..."`, or the full-path `"...\bash.exe" -c "..."` shape) never matched any classifier pattern (2206 production invocations, only 1.2% filtered) because `Detector::detect()` only prefix-matched the OUTER wrapper string; it now unwraps `bash`/`sh`/`zsh`/`dash -c` wrappers (incl. a leading `export VAR=...;` preamble) before classifying the real inner command. All five fixes are TDD (RED confirmed before each implementation), deterministic, zero-LLM. **2291/2291 tests ✓.**

## v2.15.0

**2026 feature-research backlog: seven deterministic (no-LLM) memory & agent features.** `icmg emit-agents-md` syncs an icmg-first routing block into `AGENTS.md` so non-Claude agents (Cursor, Copilot, Codex, Aider, Gemini CLI) inherit icmg-first behavior. **Bi-temporal fact invalidation** — facts carry `valid_from`/`invalidated_at`; a superseded fact is kept for history but excluded from recall (`icmg memory invalidate`). **Causal-fact retrieval** — typed causal edges + a 1-hop recall expansion over BM25 surface a cause that doesn't lexically match the query (`icmg memory link`, `recall --causal`). **Two-tier recall scheduling** — cheap BM25 by default, escalate to the ~5-6s semantic tier only for hard queries (`recall --auto-tier`). **Pre-flight prompt rewrite** — `icmg agent --rewrite` honesty-gated context compression with the system prompt + task protected verbatim. `icmg compress-prompt` exposes that honesty-gated salience compressor as a first-class op. `icmg skill-bank` distills successful command trajectories into a bounded, reusable skill bank with success-rate attribution. Plus an `ICMG_NO_DAEMON` escape hatch fixing a recall hang on a half-open daemon pipe. **Full suite 2260/2260 pass.**

## v2.14.0

**Token-efficiency arsenal: seven new levers to cut recurring spend.** A cohesive pass at the biggest structural costs of an agent session — noisy command output, transcript re-send, and per-turn tool schemas. (1) **`icmg run --nano`** — symbol-only compression: build/test/lint diagnostics collapse to one dense line each, `file:kind:code:line` (e.g. `src/main.rs:err:E0423:12`), across gcc/clang/rustc + MSVC, with order-preserving dedup. Targets 95%+ savings on repeat builds. (2) **`icmg run --gist`** — a one-line, domain-aware TL;DR of command output (`12 passed, 3 failed. first fail: user.rs:45` / `+42 -15 across 6 file(s)` / `3 commit(s): …`), heuristic and sub-millisecond, no LLM. (3) **`icmg learn`** — cross-session learning: mines the persistent `commands` table (frequency + output-line stats accumulated across every session) to flag commands that are consistently noisy — frequently run, large output, mostly filtered — and recommends a tighter mode per command. (4) **`ICMG_AUTO_ROUTE=1`** — turns that advice into action: `icmg run` auto-applies `--nano`/`--gist` to a command with a learned "noisy" verdict (opt-in; default off = unchanged behaviour). (5) **`icmg transcript cost`** — re-send amplification analysis: an agent re-sends the entire prior transcript every turn, so an entry at position *i* of *N* is paid for *(N−i)* times; this quantifies raw vs true re-send tokens, the amplification factor, and ranks the hotspots to compact first (large *early* entries dominate). (6) **`icmg mcp audit`** — tool-schema diet: measures the per-turn token cost of every registered MCP tool's name + description + schema, totals the fixed budget, and flags disproportionately large tools as trim candidates. (7) **Recall tier tie-breaker** — memory tier (hot/warm/cold) now breaks *near-equal* recall scores in favour of hotter memories (opt-in `ICMG_RECALL_TIER_BOOST`), implemented as a stable tie-breaker rather than a score multiplier so it provably cannot regress well-separated rankings. All pure-logic cores are unit-tested; **+34 tests, ctest 100%, build green (Windows).**

## v2.13.2

**Fix: err126 root cause on Windows Server (clock_cast → icu.dll).** Traced a recurring err126 to `std::chrono::clock_cast` pulling in `icu.dll`, which is absent on stock Windows Server 2019; two call sites (`bundle_cmd`, `graph_cmd`) now use direct epoch arithmetic in `file_time.hpp` instead. Added a throw-site diagnostic (`throw_site.hpp/cpp`) and a `doctor` write-probe for faster future triage. **1331 tests ✓.**

## v2.13.1


**Fix: err126 recurrence on Windows Server despite the v2.13.0 Vulkan gate.** The v2.13.0 headless gate checked only that the registry key `HKLM\...\Khronos\Vulkan\Drivers` had ≥1 value — but each value's *name* is the path of the driver's JSON manifest, and an uninstalled/stale GPU driver commonly leaves the registry value behind after the manifest file is gone. On such hosts the gate false-positived ("ICD present"), `llama_backend_init()` ran, `vkCreateInstance` failed, and the process still crashed with err126. Two-layer fix: (1) **Manifest-on-disk verification** — `vulkanIcdPresent()` now enumerates the registered manifest paths and requires at least one to actually exist on disk (no-throw `fs::exists`); stale entries no longer count as presence. The decision core (`anyIcdManifestPresent`) is pure and unit-tested (stale-only → absent, mixed live+stale → present, empty → absent). (2) **Error boundary around backend init** — the local LLM is an optional subsystem, so `llama_backend_init()` is now wrapped in try/catch in the `LlamaRunner` ctor: any init failure (e.g. `vk::SystemError` from a broken driver) degrades to "LLM unavailable, command continues" instead of killing the whole command. Instant host-side workaround (no upgrade needed): set `ICMG_GGML_NO_VULKAN=1`. **2166/2166 tests (+4 new).**

## v2.13.0

**WASM language extractors + host-caps: teach icmg a new language by dropping a `.wasm`, no rebuild.** Extends the sandboxed WASM skill system (previously filter-only) with a second ABI and a host-call seam. (1) **`extractor-v1` ABI:** a registered `.wasm`/`.wat` module can now act as a language extractor — it receives a source file and returns a JSON `{context, imports, namespaces, classes, functions, tables}` that maps 1:1 to icmg's `ExtractResult` and feeds straight into the code graph. Module exports `icmg_alloc(len)->ptr` + `icmg_extract(ptr,len)->i64` (packed `outPtr<<32|outLen`), mirroring the proven `filter-v1` calling convention; output is parsed defensively (non-string array entries dropped, malformed JSON fails open to an empty result — a broken skill degrades to "no symbols", never a crash). (2) **Auto-applied on scan:** `graph scan`/`graph update` loads registered extractors once per scan and consults them — an extractor fills a language gap (no built-in) or, with `priority > 0`, overrides a built-in. Manifest carries `language`, `extensions`, `priority`, and a `min_icmg` semver floor. (3) **CLI:** `icmg skill wasm add/list` now recognize both filter and extractor manifests by kind; new `icmg skill wasm test <name> --file <src>` runs an extractor over a source file and prints the extracted symbols. (4) **Host-caps enabler:** modules may optionally `(import "icmg" "log" (func (param i32 i32)))` — `icmg.log(ptr,len)` sends a memory slice to the host for observability, defined via a wasmtime linker. Fully backward-compatible: import-less modules instantiate exactly as before, and on an older `wasmtime.dll` lacking the linker symbols host-caps silently disables while core filter/extractor keeps working. This is the seam for future read-only host calls (`icmg.recall`, `icmg.graph_symbol`) — the sandbox stays strict; only explicitly-defined functions are reachable. Build safety-net: `build.ps1` now ensures `wasmtime.dll` + its `libzstd.dll` dependency sit next to the raw build-dir exe. **2162/2162 ctest (Windows), +30 new tests.**

## v2.12.0

**Fix: `icmg find` Unicode crash + build.ps1 auto-installs the live binary.** (1) **Unicode crash fix (issue #221):** `icmg find` crashed with Windows error 1113 ("no mapping for the Unicode character exists in the target code page") when a scanned path contained non-ANSI filenames. Three call sites in `find_cmd.cpp` that passed paths through `.string()` (which lossily narrows to the active code page) now use `pathU8()` for UTF-8-safe handling of user-controlled filenames. (2) **build.ps1 live-bin install:** after a successful build, `build.ps1` now auto-updates the in-use `~/bin/icmg.exe` via a Windows-safe rename→copy→kill→cleanup sequence (rename old to `.old` so the open handle releases the name, copy the fresh exe into the freed slot, terminate any running `icmg.exe`, then delete `.old`). No more manual copy step — and no "file in use" failure. (3) **Embedded migration parity:** migration `0046` (delta-output snapshot columns on the `commands` table) is now embedded in shipped binaries, closing an embedded-drift gap.

## v2.11.2

**Ergonomics: close three RAW=1 escape holes so the model picks icmg instead of shelling out.** Born from a 2026-06-28 audit of 3,353 wrong native-tool instincts (419 genuine RAW=1 bypasses). (1) **`icmg calc "<expr>"`** — an offline arithmetic/stat evaluator (recursive-descent: `+ - * / %`, right-assoc `**`, `sqrt/abs/floor/ceil/round/log/ln/exp/sin/cos/tan`, variadic `min/max/sum/avg`, `pi`/`e`). Closes the ~40× python/node throwaway-compute escape. Pure logic in `calc_eval.hpp`. (2) **`icmg context <file> --head N` / `--tail N`** — first/last N lines, line-numbered and memory-aware, replacing native `head`/`tail` (~47× escape). (3) **Pipe-aware bash redirect** — the 2026-06-28 search audit found 82% of search escapes were grep used as a *pipe filter* (`ls|grep`, `curl|grep`), which is not codebase search and where `icmg find`/graph structurally don't apply. A new pipe-aware classifier (`bash_redirect.hpp`, exposed via `icmg hook bash-advice`) now **exempts** a pipe ending in a self-filter (grep/head/tail/wc/sort/uniq), gives a **correctly-quoted** `icmg run "<pipe>"` redirect for non-filter pipes (the old unquoted advice leaked the tail stage back to native), and per-tool advice for single commands (`sed`→fuzzy-edit, `python`→calc, `tail/head`→context). Two evaluator bugs found + fixed by adversarial review (`-2**2` precedence, malformed `1..2`). **+5 test files, ctest 100%.**

## v2.11.1

**Fix: disable popup-killer auto-start in hooks.** The B:/ MSYS drive-letter popup is now prevented at source via `MSYS_NO_PATHCONV=1` + `MSYS2_ARG_CONV_EXCL=*` in `build.ps1`, so the popup-killer daemon is no longer needed on startup. Two auto-invocations of `icmg popup-killer ensure` (in `SessionStart` and `UserPromptSubmit` hooks) have been commented out. This eliminates the risk of the daemon auto-dismissing legitimate Win32 dialogs (Save As, Print, etc.) from Firefox, Edge, SSMS, and other apps. Manual invocation via `icmg popup-killer run` remains available if ever needed.

## v2.11.0

**Covenant + task store: deterministic cross-session injection.** (1) **`icmg covenant add/list/inject`:** a must-hold rule store that emits ALL active covenants on every SessionStart and post-compact â€” full enumeration, never BM25-sampled, never silently truncated. `--max-items` cap always emits a visible `[+N more]` marker. Empty store emits nothing. (2) **`icmg task add/list/doing/done/reopen/inject`:** a parked-work-item store that survives across sessions and compaction. `inject` outputs open tasks (`todo`+`doing`) with `doing` listed first; done tasks are silent. (3) **Hook wiring:** `icmg-covenant-task-session.sh` fires on every SessionStart and post-compact. Opt-out via `ICMG_NO_COVENANT_TASK=1`. Migration `0045` adds `covenant` + `task` tables with zone partitioning, priority ordering, and soft-enable/disable. **2027/2027 ctest (Windows + Linux), +17 new tests.**
## v2.10.0

**Graphify parity: a natural-language queryable knowledge graph, markdown/shell extractors, and edge-confidence labels.** (1) **`icmg graph-query query "<NL>"`:** ask the code graph in plain language �?" seeds from FTS search, expands a bidirectional BFS subgraph (depth + max-node capped), and answers via the local LLM when available, else prints the raw subgraph (`--no-llm`/`--json`, always offline-safe). **`icmg graph-query explain "<node>"`** describes a node and its 1-hop neighbors. Path and report intentionally reuse the existing `graph-path` / `graph-report` (no duplicate commands). (2) **Markdown + shell symbol extractors:** `.md`/`.markdown`/`.rst`/`.txt` map H1/H2/H3 to symbols and `[text](target)` / `[[wikilink]]` to graph edges; `.sh`/`.bash`/`.zsh`/`.ps1`/`.psm1` map `function foo()` to symbols and `source ./lib.sh` to edges. (3) **Edge-confidence labels:** every `graph_edges` row now carries `EXTRACTED` (direct parse) / `INFERRED` (name-match heuristic) / `AMBIGUOUS` (low-confidence) so the query engine knows how much to trust each link. (4) **Router precision fix:** `icmg suggest` / `map` / `ask` now match curated synonym keywords, so "who calls this function" routes to `graph-callers` (not `graph-callees`). (5) **De-dup:** `graph-clean` is now a thin alias for `graph-prune` (was a byte-for-byte duplicate). Also fixed a latent Phase-1 bug where the edge-confidence migration was mis-placed (global vs project DB + missing from embedded migrations), so the column is now created in shipped binaries. **2010/2010 ctest (Windows + Linux), +12 new tests.**

## v2.9.0

**Graph-traversal find, graph clustering `--project` mode, and Leiden community detection.** (1) **`icmg find --depends-on <file>`:** BFS forward-closure via GraphStore — lists all files that `<file>` transitively depends on. (2) **`icmg find --used-by <file>`:** BFS reverse-closure — lists all files that transitively depend on `<file>`. Both support `--depth N` (default 10). (3) **`icmg graph-cluster --project`:** pulls live edges from GraphStore and runs Leiden clustering, grouping files into communities with `--top-n` and `--json` output. (4) **Tree-sitter C++ extractor:** namespace-qualified symbol names, struct `kind='struct'`, full AST coverage. 5 new TDD tests each. **1983/1983 ctest (Windows + Linux).**

## v2.8.4

**Context versioning, compact handoff, perplexity salience scorer, and full gitignore-style `.icmgignore`.** (1) **`icmg compact`:** synthesizes a compact session-handoff summary from recent memory nodes + `.remember/now.md` / `.remember/recent.md` into `.icmg/compact-handoff.md` (and stdout); saves 60–80% tokens vs re-reading full history on session resume. (2) **`icmg context-commit / context-branch / context-merge`:** Git-style context versioning stored in `.icmg/ctx-vcs/` — snapshot the current context bundle as a timestamped JSON commit, switch to a named branch to isolate a reasoning path, then merge two branch snapshots; inspired by the GCC-paper finding that structured context versioning improves SWE-Bench scores 80%+. (3) **`icmg shrink --scorer=llama`:** LLMLingua-style perplexity salience scoring slot; routes through the local llama backend when available, falls back to heuristic. (4) **`.icmgignore` full glob support:** upgraded from exact-path + `*.ext` only to full gitignore semantics — `**`, `?`, `!negation`, `dir/*` — with last-rule-wins matching, same feature set as `.gitignore`. 10/10 new tests. **1962/1962 ctest (Windows + Linux).**

## v2.8.3

**Seven token-efficiency commands in one batch — read smarter, shrink harder, diff leaner.** (1) **`icmg smart-read <file>`:** auto-picks the read strategy by file size: `>50 KB` → symbols-only list (no body), `10–50 KB` → first 100 lines, `<10 KB` → full — so the right amount of context arrives without tuning. (2) **`icmg batch-read <f1> <f2> …`:** reads N files in a single tool call, separated by `=== <file> ===` dividers (`--limit N` lines per file), collapsing N sequential reads into 1. (3) **`icmg context --symbols-only`:** emits the symbol list only (no body, no memory) — ~80% token savings on large files. (4) **`icmg context --skeleton`:** emits only the first line of each symbol (function signatures without body). (5) **`icmg shrink --aggressive`:** drops comments, collapses blank runs, and abbreviates `std::` tokens; pairs with `--kind salience --scorer=llama` for perplexity-guided line selection. (6) **`icmg diff-summary --compact`:** strips context lines from diff output, keeping only `+`/`-` and `@@` headers — smallest possible diff for AI review. 8/8 new tests. **1952/1952 ctest (Windows + Linux).**

## v2.8.2

**Five new commands — fuzzy patch, time-bounded recall, memory export, context re-read delta, and intent-ranked find.** (1) **`icmg patch <file>`:** applies a unified diff to a file with ws-tolerant matching (same L1/L2/L3 cascade as fuzzy-edit); safer than raw `patch` on AI-generated diffs that drift whitespace. (2) **`icmg recall --since <date>`:** filters recall output to nodes created/touched after a timestamp (`2026-06-01`, `7d`, `yesterday`) — lets you see only what changed recently without wading through the full corpus. (3) **`icmg memory export`:** dumps the full memory store to JSON/CSV for backup, offline search, or migration. (4) **`icmg context <file> --diff`:** pack_delta mode — sends only lines changed since the last context read of this file (60–80% savings on iterative re-reads). (5) **`icmg find` PageRank boost:** intent search now ranks results by Personalized PageRank seeded from the query terms, so the files that actually matter for the task surface first. **1944/1944 ctest (Windows + Linux).**

## v2.8.1

**`icmg fuzzy-edit` — whitespace-tolerant file edits that don't fail on indent drift.** The standard `old_string` / `new_string` edit fails when an AI emits the right code but wrong indentation (2-space vs 4-space, trailing spaces). `icmg fuzzy-edit <file>` runs a three-level cascade: **L1** exact substring (with a line-boundary guard that prevents a 2-space pattern matching inside a 4-space line), **L2** whitespace-normalized per-line match (strips leading/trailing whitespace per line, preserves the file's actual indentation in the replacement), **L3** anchor-line match (first non-empty line of old_string as anchor, replaces N lines). CRLF-transparent; `--dry-run` to preview; Jaccard closest-match hint in error messages. 6/6 TDD tests. **1938/1938 ctest (Windows + Linux).**

## v2.8.0

**Three KV-cache-aware token-saving wins -- icmg now tells you when your prompt stops caching, keeps its tool list stable, and turns spilled output into an actionable pointer.** Grounded in the context-engineering finding that KV-cache hit rate is the #1 cost lever (cached input is billed ~10% of fresh), this release closes the loop measure -> optimize -> measure. (1) **Cache-hit advisor (`icmg savings --cache-advisor`):** the token ledger already showed an aggregate cache-hit rate; the advisor reads it as a **trend**, splitting per-turn samples into a prior vs recent half and comparing mean hit-rate. A meaningful drop flags that **volatile content leaked into the cached prefix** (a per-turn timestamp, a memory-inject that changes every turn) and is busting the cache -- with the concrete fix (move volatile content to the end of the user turn, keep the prefix append-only). Auto-prints on a degrading verdict; on real data (760 turns) it reports `cache-hit stabil di 94%, prefix konsisten`. (2) **Stable tool list (anti KV-cache bust):** `Registry::keys()` iterated an `unordered_map`, so its order was nondeterministic between builds/runs. The MCP server advertises the tool list every connection and those definitions sit in the AI client's **cached prompt prefix** -- a reshuffled order silently busts the client's KV-cache. `keys()` is now sorted once, so every enumeration (incl. `tools/list`) is stable by default; callers that already sorted become harmless no-ops. (3) **Filesystem-as-context -- actionable spill reference:** `capOutput` already spilled over-budget output to a temp file, but the footer was a dead-end (`N bytes spilled to <path>`). It is now a **just-in-time pointer**: it names the path, states the full line count, and tells you exactly how to retrieve it (`Read <path> (use offset/limit to page) -- do NOT re-run the command`), so a large observation lives on disk while the context holds only a cheap, actionable reference. **1925/1925 ctest (Windows + Linux), +11 new tests.**
## v2.6.0

**`icmg compress` now learns, self-seeds, and forgets -- a self-improving compression vocabulary that stays relevant over time (Adaptive Output Gate).** Compression used to rediscover its glossary from scratch on every call, so high-value recurring phrases (paths, long identifiers, boilerplate) that appeared only once in a given input were never substituted. The new Adaptive Output Gate closes that loop end-to-end. (1) **LearnedGlossary** -- a cross-session SQLite vocabulary (`CREATE TABLE IF NOT EXISTS`, no migration) that records which glossary aliases actually recur, accumulating hits + attributed token-savings per phrase; learning survives session boundaries. (2) **Self-seeding:** `compress` loads `LearnedGlossary.suggest(min_hits)` into the compressor's new `seed_phrases` before each run, substituting proven phrases via a distinct `@S<n>` alias namespace **even at per-call frequency 1** -- which per-call discovery (min-freq 5) could never catch. Round-trip stays lossless (`@S` added to the strict expand leftover-check); `--no-seed` opts out; `compress.seed_min_hits` config (default 3, <=0 disables). (3) **Durable, not just bigger:** the glossary was append-only -- `last_seen` was tracked but never ranked, so stale high-savings phrases permanently outranked fresh ones and the table flooded with noise. Now `decayedValue()` applies an exponential half-life (30-day default in the live seed path) so recently-used vocab rises and stale sinks, and `prune()` forgets dead weight (old **AND** below min-hits; high-hit phrases never aged out), self-maintained per run via `compress.seed_prune_days` (default 90). Mirrors the `imem` BM25+recency+importance model. (4) **Compress-before-cap** output gate (`icmg context --gate`) emits a lossless glossary instead of a lossy `truncated` footer when output would exceed the byte cap. Seed count is folded into the tool-call cache key so a changed vocabulary never serves stale output. 1870/1871 ctest (1 pre-existing order-dependent skill-manifest flake, passes isolated).
## v2.5.1

**`icmg verify --help` works from any directory -- no `.icmg/` required.** `VerifyCommand::run()` opened the project SQLite DB before checking for `--help`, so `icmg verify --help` (and any invocation from a directory without a `.icmg/`, e.g. a fresh checkout or `ctest` run from `build/`) failed with `unable to open database file` instead of printing usage. The `--help` early-exit now runs *above* the `core::Db` constructor. Hardened the verify_cmd test with a `VerifyDbGuard` RAII that redirects the project DB to a local `*_test.db` and pre-creates the `verifications` table, so the suite no longer depends on ambient cwd state. 1857/1857 ctest (Windows + Linux).
## v2.5.0

**Memory recall grew up: citable, time-aware, and disclosure-first -- plus a privacy redactor and a store that tags itself.** A batch of recall/memory UX upgrades. (1) **Citable recall:** `icmg recall` prints `[score] #id <icon> topic` by default so any result can be referenced by id, with a typed icon (decision/fix/note) at a glance. (2) **Progressive disclosure:** `recall --index` then `recall --get <id>` lets you skim a compact index and pull a full node only when needed; `recall --timeline` gives a chronological day-grouped view (newest first); `recall --by file` buckets memories by the source file they mention. (3) **Store safeguards:** a `<private>...</private>` redactor strips secrets before persistence, and automatic keyword derivation (stopword-filtered, deduped, capped at 6) kicks in when `--kw` is omitted. (4) **wake-up** gains a typed-icon legend, per-decision icons, and a token-cost footer. (5) **Operability:** `icmg serve` exposes `GET /api/health` (status/uptime/db/counts/version JSON); `icmg install` does smart version caching -- skips a reinstall when running and installed versions match (`--force` / `--status`). (6) **`icmg init` auto-wires grep hook:** generates `.claude/grep-hook.js` + merges `PreToolUse[Grep]` entry into `.claude/settings.json` (idempotent, upgrade-safe). (7) **Graph `--since` mtime fix (Windows):** `file_time_type` uses NTFS epoch (1601-01-01); comparing its duration directly to a unix-epoch cutoff was off by ~116 years, causing `--since` to always include all files. Fixed via `clock_cast` (C++20) or portable `stat()` fallback (C++17). (8) **Graph `--parallel` no longer deadlocks:** previously spawned N subprocesses writing the same SQLite DB -- large change-sets (>32 files) hit `database is locked`; collapsed to a single in-process Scanner that batches all changed files sequentially (no cross-process DB contention). `--force` added to trigger full xref rescan. 1818/1818 ctest (Windows + Linux).
## v2.4.2

**`icmg run` works on plain Windows (no git-bash), plus graph-precision + store-latency fixes.** (1) **non-MSYS `icmg run`:** was fully broken on Windows without git-bash (`CreateProcess failed: 2`) -- relied on CreateProcess PATH-search that misses `System32\WindowsPowerShell\v1.0` + the Store `pwsh` alias. Now resolves the shell to a FULL path (`resolveWinShell`) used verbatim, and the not-found retry falls back through PowerShell for builtins/cmdlets (echo/dir/Get-ChildItem). A whole platform config goes broken -> working. (2) **Graph edge-noise:** a `call:<name>` resolved to an edge for EVERY same-named symbol; common names (run/get/value) fanned out into dozens of false `calls` edges inflating unrelated/third_party PageRank. `filterCallTargets` now leaves a name with > 4 definitions unlinked. (3) **Store latency:** dup-check scanned the whole corpus (O(N) Jaccard) every store; bounded to recent 500 + hoisted the query tokenizer + git-sha now read from `.git/HEAD` (no subprocess). 1763/1763 ctest (Windows + Linux).
## v2.4.1

**`icmg run "<shell line>"` works again -- pipes, `&&`, and redirects no longer break.** Two long-standing bugs in how `icmg run` handled a single quoted command string. (1) A whole-command token like `icmg run "echo hi"` was *re-quoted*, so the underlying `bash -c` saw `"echo hi"` as one word -> `command not found`; fix: a single command token is now a verbatim shell line. (2) A piped command like `icmg run "ls | grep x"` had its `|` split into a literal arg fed to `ls`; fix: new `hasShellOperators()` routes any command with an *unquoted* `|` / `&&` / `;` / `>` / backtick / `$(...)` through the shell instead of argv-exec -- for both the buffered and `--stream` paths. Quoted operators (`grep 'a|b' f`) stay literal on the fast argv-safe path. 1755/1755 ctest (Windows + Linux).
## v2.4.0

**The code graph now ranks by PageRank -- and you can steer it at your task.** (1) **PageRank ranking:** `graph skeleton` and `graph recent` ranked files by 1-hop *degree* (bundled headers dominated); they now rank by **PageRank** -- importance propagates transitively (a symbol referenced by important symbols outranks one referenced by many trivial ones, the aider/RepoGraph ranking) and each edge is **confidence-weighted** (`inherits` > name-based `calls`). Pure power-iteration, no new dependency. (2) **Task-personalized:** `graph skeleton --for "<task>"`, `graph recent --for "<task>"`, and `icmg pack "<task>" --pr` seed a **Personalized PageRank** from the task terms so the most relevant code surfaces first; pack's default path stays fast (no full-graph load). (3) **Clean by default:** vendored/third_party files, sibling projects that leaked in via cross-project edges, and test files are filtered from graph views (`--include-vendored` / `--all-paths` / `--tests` to include), so a skeleton maps *your* production code. 1750/1750 ctest (Windows + Linux).
## v2.3.1

**The B:/ "cannot find drive" popup killed at the source, plus a self-enforcing memory gate that actually fires.** (1) **B:/ popup root fix** -- the modal "insert disk" popup (and its beep) that flashed during `icmg recall`/`context` under git-bash is gone at the *root*: MSYS's path-converter was rewriting cmd.exe flags that look like POSIX paths into drive paths (`/B` -> `B:\`), and the nonexistent B: drive made cmd raise a modal dialog; `icmg` now sets `MSYS_NO_PATHCONV=1` in its own process environment so child shells pass flags verbatim and the popup is never *created* -- not merely dismissed after the fact, which always raced and flashed. (2) **Ritual-gate recorder fix** -- the post-change gate that nudges `store`+`wflog` after a code edit was misfiring every turn because its recorder never ran: a missing newline in the generated Bash hook (`fi` and `out=` fused into `fiout=`) syntax-errored the whole hook, and even when it ran it inspected only the first token of a command starting literally with `icmg `, so `RAW=1 icmg store` (env prefix) and `icmg store && icmg wflog` (chained) were never recorded. Both fixed with a pure, unit-tested command-line parser that strips env-var prefixes and scans every `&&`/`||`/`;`/`|` segment, plus a new `icmg ritual saw-line` action. 1726/1726 ctest (Windows + Linux).
## v2.3.0

**Four more languages with real symbol extraction, plus ground-truth observability.** (1) **Kotlin, Swift, Ruby, and Scala** now get first-class *symbol* extraction (classes/objects/traits/interfaces/enums/functions + their base types), not just import edges — via lean **regex** extractors in the proven csharp/sql style, so there is **zero grammar bloat** (the Kotlin tree-sitter grammar alone is 22-34 MB; Swift ships no committed parser.c at all). (2) **`icmg whereami`** prints one authoritative snapshot — the running binary + version, the config file actually in use, and the project/global/persona DB paths — so a stale binary or a wrong-path guess is caught in one command instead of a debugging detour. (3) **Config path fix:** `icmg config set/get` write to the platform global dir (`%APPDATA%/icmg` on Windows) but `unset`/`edit`/`zone`/`list` had hardcoded `~/.icmg`; they now agree, and **`icmg config path`** shows exactly where config lives. 1718/1718 ctest (Windows + Linux).
