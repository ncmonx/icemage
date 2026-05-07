# Phase 23 — Semantic Recall + Agent + MCP Resources + Chat REPL

**Goal:** finish the AI-track deferred from Phase 21. Add semantic embeddings (BM25 paraphrase blind spot), an LLM agent proxy, MCP Resources protocol exposure, and an interactive REPL.

**Why:** these are the four items deferred from Phase 21 because each needed external dependencies (Python sidecar / LLM API / full MCP protocol / interactive loop). Phase 23 takes them in dependency order with graceful-fallback design so existing functionality never regresses.

**Tech:**
- Embeddings: Python sidecar process (sentence-transformers `all-MiniLM-L6-v2`, 384 dim) OR ONNX Runtime tier-2.
- Vector store: `sqlite-vec` extension (preferred) OR BLOB columns + manual cosine.
- Agent: `core/exec_utils::safeExec` to call user's chosen LLM CLI (`claude`, `gh copilot suggest`, `ollama run`, custom curl wrapper).
- MCP Resources: extend `src/mcp/server.cpp` JSON-RPC handler.
- REPL: `linenoise` single-header lib (or std-cin loop fallback).

**Estimate:** 8-12 days total. Tasks parallelizable.

**Assumptions:**
- Python 3.9+ on `$PATH` for tier-1 embeddings (graceful fallback to BM25-only when missing).
- LLM CLI is user-supplied — icmg never bundles model weights or API keys.
- MCP Resources protocol stable per MCP spec 2025-06-18.

---

## Task 1 — Embedding pipeline (sidecar)

**Files:**
- Create: `src/embed/embedder.hpp` — interface.
- Create: `src/embed/embedder_python.cpp` — spawns Python sidecar, JSON stdio.
- Create: `embed/icmg_embedder.py` — sentence-transformers loader, batch process loop.
- Schema: `migrations/0010_embeddings.sql` — `embeddings(node_id, kind, vec BLOB, dim INT, model TEXT, created_at)`.

**Sidecar protocol (JSON over stdio):**
```json
// Request:
{"op": "embed", "id": 42, "text": "..."}
// Response:
{"id": 42, "vec": [0.12, -0.05, ...], "dim": 384}
```

**Lifecycle:**
- Lazy-spawn on first embed call. Reuse for whole CLI invocation.
- Graceful no-Python fallback: log warning, fall back to BM25-only paths.
- On `icmg memory store` and `icmg graph scan`: enqueue embed job (best-effort; main flow doesn't block on embed).

**API:**
```cpp
class Embedder {
public:
    virtual ~Embedder() = default;
    virtual bool available() const = 0;
    virtual std::vector<float> embed(const std::string& text) = 0;
};
std::unique_ptr<Embedder> makeEmbedder();  // factory; returns null when missing
```

**Verification:**
- `python3 -c "import sentence_transformers"` → tier-1 ready
- `icmg memory store "X"` → embedding written to `embeddings` table within ~50ms
- Missing Python → silent fallback, no crash, no regression in existing tests
- Test: `test_embedder_smoke` — store + read back vec, dim=384

**Estimate:** 2-3 days.

---

## Task 2 — Vector recall hybrid

**Files:**
- Modify: `src/imem/memory_store.cpp::recall()` — add `--semantic` and `--alpha N` params.
- Create: `src/embed/vec_search.cpp` — cosine similarity loop over candidate set.

**Hybrid score formula:**
```cpp
score = α * bm25_normalized + (1 - α) * cosine(query_embed, doc_embed);
// default α = 0.5
```

**Strategy:**
1. BM25 returns top-50 candidates (cheap).
2. For each, fetch embed from `embeddings` table.
3. Compute cosine; rerank.
4. Return top-K.

**CLI:**
```bash
icmg recall "X" --semantic           # default α=0.5
icmg recall "X" --semantic --alpha 0.3   # weight more toward vec
icmg recall "X" --semantic --pure        # alpha=0 (vec only)
```

**Fallback:** if no embeddings indexed yet OR Embedder unavailable → BM25 results unchanged + warning on stderr.

**Verification:**
- Paraphrase test: store "fix login bug", recall "auth issue" — semantic finds it, BM25 misses
- `--alpha 0` returns vec-only ranking; `--alpha 1` returns BM25-only ranking
- Test: `test_vec_search` (cosine math, hybrid blending, fallback)

**Estimate:** 1-2 days.

---

## Task 3 — `icmg agent <task>`

**Files:**
- Create: `src/cli/commands/agent_cmd.cpp`
- Modify: `src/core/config.hpp` — agent.provider, agent.command, agent.api_key_env.

**Flow:**
1. `icmg pack <task>` → context bundle (≤4KB).
2. Build prompt: bundle + task description + system prompt template.
3. Call configured LLM via `core::safeExec`.
4. Capture stdout. Filter: keep code blocks + decisions sentence.
5. Auto-store decision via `icmg store --topic decisions-<project>`.

**Config:**
```json
// ~/.icmg/config.json
{
  "agent": {
    "command": "claude --print",
    "model": "sonnet",
    "max_tokens": 2000,
    "system_prompt_path": "~/.icmg/agent-system.md"
  }
}
```

**CLI:**
```bash
icmg agent "review this PR for race conditions"
icmg agent --dry-run "..."        # show packed prompt without calling
icmg agent --no-store "..."        # don't auto-memorize
```

**Hard token cap:** `agent.max_tokens` (default 2000) prevents API cost spike. Logs every call into `tool_invocations` (tool='agent').

**Security:**
- `agent.command` is shell — user's responsibility. Document risk.
- Never read `~/.icmg/secrets.json`; agent code reads env var named in `agent.api_key_env` only when child cmd needs it.

**Verification:**
- `icmg agent --dry-run "test"` → shows full prompt, exits 0
- With Ollama configured: `icmg agent "explain X"` → returns + stores decision
- Test: `test_agent_dryrun` (prompt assembly, no real API call)

**Estimate:** 2 days.

---

## Task 4 — MCP Resources protocol

**Files:**
- Modify: `src/mcp/server.cpp` — add `resources/list`, `resources/read` handlers + capabilities advertisement.
- Create: `src/mcp/resources/graph_resource.cpp`, `memory_resource.cpp`, `context_resource.cpp`.

**URI scheme:**
```
icmg://graph/<project>/<node_id>      → returns full GraphNode JSON
icmg://memory/<id>                     → MemoryNode JSON
icmg://context/<file_path>             → bundle (Phase 19 `icmg context`)
icmg://summary/<file_path>             → outline (Phase 20 `icmg summarize`)
icmg://session/<name>                  → snapshot
```

**`resources/list` handler:**
- Returns top-N hot files (+memory) as available resources.
- Pagination via `cursor`.

**`resources/read` handler:**
- Parses URI, dispatches to relevant store, returns JSON content.

**Capabilities:**
```json
"capabilities": {
  "resources": {"subscribe": false, "listChanged": false},
  "tools": {"listChanged": false}
}
```

**Why this matters:** Claude Desktop / Code reads resources via standard protocol → client-side cached, no per-call serialization round-trip. Especially valuable for repeated reads of same file's context.

**Verification:**
- MCP Inspector shows resources in resource picker
- `resources/read icmg://graph/myproj/42` returns valid JSON
- Test: `test_mcp_resources` (URI parsing, dispatch, JSON shape)

**Estimate:** 1-2 days.

---

## Task 5 — `icmg chat` REPL

**Files:**
- Create: `src/cli/commands/chat_cmd.cpp`
- Add: `third_party/linenoise/linenoise.{h,c}` (~600 LOC, MIT, no deps).

**Behavior:**
- Each user prompt → `icmg pack` first → bundle prepended to LLM call.
- LLM response printed; auto-stored as memory with `topic="chat-<session>"`.
- `\save <name>` → `icmg session save <name>`.
- `\load <name>` → restore + show recent.
- `\clear` → reset working context (memory remains).
- `\help` → command list.
- `\quit` / Ctrl+D → exit.

**Persistence:**
- History in `~/.icmg/chat-history.txt` (linenoise-managed).
- Each session gets unique `<name>` if not specified (timestamp-based).

**Dependencies:** Task 3 (agent) for actual LLM call. Without Task 3, `icmg chat` runs as packing-only sandbox (still useful for testing).

**Verification:**
- Interactive: launch, type query, see bundled prompt + LLM response
- `\save / \load` round-trips
- History persists across launches
- Test: `test_chat_dryrun` (REPL command parsing without actual LLM)

**Estimate:** 2 days.

---

## Task 6 — Optional ONNX Runtime path (tier-2)

**Files:**
- Create: `src/embed/embedder_onnx.cpp`
- Add to CMake: ONNX Runtime as optional dep.

**Why:** removes Python dependency. Same model, same dim, +20MB binary.

**When to use:** users who can't install Python. Defaults remain Python-first; ONNX is `cmake -DICMG_USE_ONNX=ON`.

**Verification:**
- Same `test_embedder_smoke` test passes with both backends
- Cosine similarity matches between Python and ONNX backends to ≤0.01 epsilon

**Estimate:** 2-3 days. Skip if Python tier-1 sufficient.

---

## Verification Checklist (cumulative)

- [ ] Python sidecar embedder produces 384-dim vectors
- [ ] BM25-only fallback works when Python missing (no test regression)
- [ ] `icmg recall "X" --semantic` finds paraphrased matches BM25 missed
- [ ] `--alpha 0` and `--alpha 1` return correct extreme rankings
- [ ] `icmg agent --dry-run "task"` emits prompt without calling LLM
- [ ] MCP Inspector lists resources via `resources/list`
- [ ] `resources/read icmg://graph/...` returns valid JSON
- [ ] `icmg chat` REPL accepts queries, calls agent, stores responses
- [ ] `\save`/`\load` round-trip preserves chat snapshot
- [ ] All existing 18+ unit tests still pass
- [ ] New tests: `test_embedder_smoke`, `test_vec_search`, `test_agent_dryrun`,
      `test_mcp_resources`, `test_chat_dryrun`

---

## Rollback / Risk

| Risk | Mitigation |
|---|---|
| Python sidecar missing on user machine | Graceful fallback to BM25-only; log warning |
| Sidecar process crash mid-session | Detect EOF, log, fall back; never crash main icmg |
| ONNX +20MB bloats default binary | Tier-2 only via `-DICMG_USE_ONNX=ON` |
| Agent LLM cost spike | `agent.max_tokens` hard cap + dry-run mode |
| MCP protocol version mismatch | Pin advertised version; reject unknown methods |
| sqlite-vec extension not available | Fallback to BLOB + manual cosine (5-10× slower but works) |
| Stale embeddings after content change | `body_hash` (Phase 18) triggers re-embed on rescan |

All tasks rollback-safe — features are opt-in via flags (`--semantic`, `agent` config, MCP capability advertisement, separate `chat` command).

---

## Dependency Graph

```
Task 1 (embedder pipeline)
  └── Task 2 (vec recall) — depends on embeddings table
  └── Task 6 (ONNX optional) — alternative embedder backend

Task 3 (agent)
  └── Task 5 (chat REPL) — depends on agent for LLM call

Task 4 (MCP resources) — independent; can run parallel to embeddings
```

**Recommended execution order:**
1. T1 embedder + T4 MCP resources in parallel (3 days)
2. T2 vec recall (1 day)
3. T3 agent (2 days)
4. T5 chat REPL (2 days)
5. T6 ONNX (optional, future session)

**Total wall-clock:** 8 days with task parallelism, 11 days sequential.

---

## Cumulative Token-Efficiency Impact (after Phase 23)

| Metric | v0.8.x | After P23 |
|---|---|---|
| Recall hit rate (paraphrase queries) | ~50% | ~85% |
| MCP serialization overhead | 100% | <10% (resource cache) |
| LLM call cost per task | full prompt | bundled prompt (50-70% smaller) |
| Cumulative token saving | 70-85% | 80-92% |

**Phase 23 closes the "BM25 misses paraphrases" gap and the "MCP repeated-read cost" gap — last two big leakage points.**

---

## Out of Scope (Phase 24+)

- Auto-fine-tune embedding model on local corpus (cool but heavy)
- Voice mode (wrong audience)
- Web dashboard mode (already covered by `icmg viz` HTML)
- Production telemetry / alerting (use Datadog)
- Multi-tenant auth (single-user local-first by design)
