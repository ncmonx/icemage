# Phase 21 — Advanced (Embeddings, Agent Proxy, MCP Resources)

**Goal:** Beyond keyword recall — semantic retrieval, agent-style auto-context, and MCP resource standard for cached graph access.

**Why:** BM25 misses paraphrases. Embedding recall + AGENT command lift relevance, push token savings into the 70-90% range while preserving (improving) context.

**Tech:** Local embedding model (sentence-transformers via Python or ONNX Runtime), MCP resources protocol.

**Estimate:** 6-8 days (was 5-7d; +1d for `icmg parallel` task 5b).

**Assumptions:**
- User accepts a Python sidecar OR ONNX Runtime binary dep for embeddings.
- Vector store is SQLite with `sqlite-vec` extension or flat-file numpy.

---

## Task 1 — Embedding pipeline

**Files:**
- Create: `src/embed/embedder.hpp`, `src/embed/embedder_python.cpp`
- Schema: `migrations/0010_embeddings.sql` — `embeddings(node_id, kind, vec BLOB, dim INT, model TEXT)`

**Approach (tier-1):**
- Spawn `python3 -m icmg_embedder` once (sidecar process, JSON stdio).
- Model: `all-MiniLM-L6-v2` (384 dim, ~80MB).
- On `icmg memory store` and `icmg graph scan`: enqueue embed job.
- Background worker batches embeds (32 docs) → writes BLOB.

**Tier-2 (optional):** ONNX Runtime C++ — no Python dep, +20MB binary.

---

## Task 2 — Vector recall

**Files:**
- Modify: `src/imem/memory_store.cpp::recall()` — accept `--semantic` flag.
- Create: `src/embed/vec_search.cpp` — cosine similarity over BLOBs.

```cpp
// hybrid: BM25 + semantic, alpha-weighted
score = α·bm25_norm + (1-α)·cosine(embed(query), embed(doc));
```

Default α = 0.5. Override via `--alpha`.

---

## Task 3 — `icmg agent <task>`

**Files:**
- Create: `src/cli/commands/agent_cmd.cpp`

**Flow:**
1. Receive task text.
2. Run `icmg pack <task>` → context bundle.
3. Call configured LLM (Anthropic/OpenAI/local) with bundle + task.
4. Capture response. Filter: keep only code blocks + decisions.
5. Auto-store decision/result via `icmg store --topic decisions-<project>`.

**Use:** `icmg agent "review this PR for race conditions"` → returns concise verdict + auto-memorized.

**Config:** `~/.icmg/config.json` keys: `agent.provider`, `agent.api_key_env`, `agent.model`.

---

## Task 4 — MCP Resources protocol

**Files:**
- Create: `src/mcp/resources/graph_resource.cpp`

Expose graph nodes as resources:
- URI: `icmg://graph/<project>/<node_id>` → returns full node data
- URI: `icmg://memory/<id>` → memory node
- URI: `icmg://context/<file>` → bundle (Phase 19)

Claude reads via standard `resources://`, client-side cached, no per-call serialization cost.

---

## Task 5 — Cross-project graph join

**Files:**
- Modify: `src/cli/commands/recall_cmd.cpp` — `--all-projects` flag.
- Modify: `src/core/global_db.cpp` — add `cross_project_search()`.

Iterate registered projects; aggregate top-K from each. Useful when same symbol exists in multiple repos.

---

## Task 5b — `icmg parallel` — subprocess fan-out primitive (NEW)

**Goal:** generic concurrent task dispatcher for icmg subcommands. Pure C++ (pipes + threads), no Claude API. Foundation for parallelizing pack/verify/recall/scan.

**Files:**
- Create: `src/core/parallel.hpp`, `src/core/parallel.cpp` — process pool + JSON merge.
- Create: `src/cli/commands/parallel_cmd.cpp` — `icmg parallel` CLI front-end.

**Why:** several icmg hot paths are I/O-bound and embarrassingly parallel:
- Scan: per-zone fan-out (4-6× speedup on monorepos)
- Pack: recall + symbol lookup + rule resolve (3-4× speedup)
- Phase verify: ctest + lint + gate + memory check (longest-task time)
- Cross-project recall: per-project BM25 fit (N× scaling)

Rather than hard-code parallelism in each command, ship one primitive.

### CLI

```bash
icmg parallel \
    --task "icmg recall 'X' --zone api --json" \
    --task "icmg recall 'X' --zone sync --json" \
    --task "icmg recall 'X' --zone ui --json" \
    [--merge json|concat|none]              # default: json (top-level array merge)
    [--max-concurrency N]                   # default: cpu_count
    [--timeout-ms N]                        # default: 60000
    [--fail-fast]                           # abort siblings on first non-zero exit
```

**Exit code:** 0 if all OK, max(child exit codes) otherwise.

### API (used internally by other commands)

```cpp
struct ParallelTask {
    std::string command;          // shell-style; safe-exec'd via Run
    int         timeout_ms = 60000;
    std::string id;               // optional tag for output
};
struct ParallelResult {
    int         exit_code;
    std::string stdout_str;
    std::string stderr_str;
    int         duration_ms;
};

// Spawns up to max_concurrency child processes; collects results in submission order.
std::vector<ParallelResult>
parallel(const std::vector<ParallelTask>& tasks, int max_concurrency);
```

### Implementation

- POSIX: fork+exec with pipes, `select()`/`poll()` for multiplexed reads.
- Windows: `CreateProcess` + named pipes; reuse `safeExecWin` from `core/exec_utils`.
- Each child runs as a fresh `icmg` invocation — independent SQLite reads (WAL mode allows N readers + 1 writer).
- Default `--merge json`: parse each child stdout as JSON array; concat results. If any child output is not valid JSON, fall back to concat with `\n---\n` separator.

### Retrofit existing commands

Behind `--parallel` flag (opt-in):

| Command | Before | With --parallel |
|---|---|---|
| `icmg pack <task>` | Sequential recall + lookup + rules | Fan-out 3 sub-icmg calls, merge |
| `icmg phase verify <num>` | Sequential checks | One sub-task per recorded verification |
| `icmg recall --all-projects` | Loop over registered projects | Fan-out per project |
| `icmg graph scan` (multi-zone) | Single-thread walk | Fan-out per top-level zone dir |

Implementation: each command checks `--parallel` flag → builds `ParallelTask` list → calls `parallel(tasks, N)` → merges output. Fallback to sequential when N < 2.

### Verification

- [ ] `icmg parallel --task "echo a" --task "echo b"` → both outputs in stdout.
- [ ] Fan-out 5 zone recalls completes in ~max(per-zone) time, not Σ.
- [ ] Timeout: `icmg parallel --task "sleep 10" --timeout-ms 1000` → exit code reflects timeout (124).
- [ ] `--fail-fast`: kills siblings when first task exits non-zero.
- [ ] JSON merge: each child produces `[{...}]`, parent emits flat array.
- [ ] No corruption when 4 concurrent readers hit same SQLite DB (WAL mode test).

### MCP exposure

`icmg_parallel` MCP tool → Claude can dispatch fan-out without invoking shell.

### Rollback

Pure additive. Default sequential for all existing commands; `--parallel` opt-in.

### Cost / Effort

1-2 days. Pays back across pack/verify/recall/scan instantly.

---

## Task 5c — DB CLI filter (sqlcmd / mysql / psql) (NEW)

**Goal:** dedicated Tkil filter for SQL Server / MySQL / MariaDB / PostgreSQL CLIs. Without this, large query results hit the default filter (first 50 + last 20 lines) which is wrong for tabular data — drops middle rows useful for verification, keeps ASCII border noise.

**Files:**
- Create: `src/tkil/filters/db_filter.cpp`
- Modify: `src/tkil/detector.cpp` — add `Db` type + patterns.

**Detection (command-string prefix or regex match):**

| Pattern | Filter mode |
|---|---|
| `sqlcmd`, `osql` | T-SQL |
| `mysql`, `mariadb` | MySQL |
| `psql` | PostgreSQL |
| `mysqldump`, `pg_dump`, `sqlcmd ... -d` schema dumps | pass-through |
| `sqlite3` | already covered by default; skip |

**Filter strategies:**

T-SQL (sqlcmd):
- Pass through `Msg N, Level N` / `Server: Msg ...` lines (errors).
- Pass through `PRINT` output (often debug info).
- Detect column header → keep header + first 20 rows + `(N rows affected)` footer.
- Multi-batch (`GO`-separated): emit per-batch summary.

MySQL/MariaDB:
- Strip ASCII border lines (`+----+----+`) but keep one as separator.
- Keep header row + first 20 rows + `X rows in set (Y sec)` footer.
- Pass through `ERROR <code> (<state>)` lines.

PostgreSQL (psql):
- Detect `(N rows)` summary line; keep header + first 20 rows + summary.
- Pass through `ERROR:` / `NOTICE:` / `WARNING:`.

**Output cap:** if filtered still > 8KB, hard-truncate via `core::capOutput` (Phase 19).

### Verification

- [ ] `sqlcmd -Q "SELECT TOP 1000 ..."` (~80KB raw) → ~3KB filtered.
- [ ] `mysql -e "SELECT * FROM logs"` (10K rows) → header + 20 rows + footer.
- [ ] `psql -c "SELECT ..."` (1K rows) → similar.
- [ ] Schema dump (`mysqldump`, `pg_dump`) → pass-through (full output).
- [ ] Stored-proc error → `Msg N` lines preserved.
- [ ] `PRINT 'debug'` rows preserved.
- [ ] New tests: `test_db_filter` (4 cases per dialect).

### Token saving

- SELECT 10K rows: ~99% reduction.
- Schema dumps: 0% (intentional).
- Errors: 0% (preserved).

### Risk / Rollback

Pure additive Tkil filter. `icmg run --raw <cmd>` still bypasses filter. Wrong detection → falls back to default first/last filter (no data loss, just suboptimal).

### Effort

1-2 hours per dialect (T-SQL + MySQL + PG). Total ~4 hours.

---

## Task 6 — Streaming filter middleware

**Files:**
- Modify: `src/tkil/rtk.cpp` — add `streamFilter()` reading stdin chunk by chunk.
- Add: `icmg run --stream <cmd>` flag (already in spec, implement).

**Use:** `npm test 2>&1 | icmg filter test` — real-time output, filter applied incrementally.

---

## Task 7 — Auto-link stored procedures

**Files:**
- Modify: `src/sp/sp_store.cpp`
- Hook: PostToolUse on Edit/Write — if file is `.sql` or `.cs` containing `EXEC`, run `icmg sp link <file>` to update graph edges.

---

## Task 8 — `icmg chat` REPL with persistent context

**Files:**
- Create: `src/cli/commands/chat_cmd.cpp`

Interactive REPL:
- Each prompt → `icmg pack` runs first → bundle injected.
- LLM call streams answer.
- After each turn, store decision/answer to memory.
- `\save mytask` checkpoints, `\load` resumes.

---

## Task 9 — Token analytics dashboard

**Files:**
- Modify: `src/viz/html_template.hpp` — new tab "Token analytics".
- Pulls from Phase 20's `tool_invocations` table.

Charts: tokens/day, savings/day, top-savers, projection.

---

## Verification Checklist

- [ ] Embedding sidecar starts; `embed_one_doc` returns 384-dim vec.
- [ ] `icmg recall "X" --semantic` finds paraphrased matches BM25 missed.
- [ ] `icmg agent <task>` returns code + auto-stores decision.
- [ ] MCP resources URIs resolve in Claude Desktop / Code.
- [ ] `--all-projects` returns aggregated top-K across global DB.
- [ ] `icmg parallel --task ... --task ...` runs concurrently, merges JSON.
- [ ] `icmg pack --parallel` finishes in ~max(per-task) time.
- [ ] `--fail-fast` aborts siblings on first non-zero exit.
- [ ] No corruption with 4 concurrent SQLite readers (WAL test).
- [ ] `npm test | icmg filter test` produces filtered streaming output.
- [ ] All existing 17 tests pass.
- [ ] New tests: `test_embedder_smoke`, `test_vec_search`, `test_agent_dryrun`,
  `test_parallel` (concurrency, timeout, fail-fast, JSON merge).

---

## Rollback

Embedding is opt-in; `--semantic` default off. Agent requires explicit config. MCP resources additive to existing tools list.

---

## Risk Mitigation

| Risk | Mitigation |
|---|---|
| Python sidecar crash | Fall back to BM25; log warning |
| LLM agent cost spike | Hard token cap per session; dry-run mode |
| Parallel SQLite contention | WAL mode + read-only child connections + serialize writes via parent |
| Subprocess fork-bomb | `--max-concurrency` cap (default = cpu_count, hard cap 32) |
| Hang on stuck child | `--timeout-ms` sends SIGTERM then SIGKILL after grace |
| Embedding staleness | Embed-on-write hook; nightly re-embed task |
| MCP cache invalidation | Watch graph_nodes mtime; bust cache on scan |
