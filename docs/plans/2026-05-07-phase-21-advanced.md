# Phase 21 — Advanced (Embeddings, Agent Proxy, MCP Resources)

**Goal:** Beyond keyword recall — semantic retrieval, agent-style auto-context, and MCP resource standard for cached graph access.

**Why:** BM25 misses paraphrases. Embedding recall + AGENT command lift relevance, push token savings into the 70-90% range while preserving (improving) context.

**Tech:** Local embedding model (sentence-transformers via Python or ONNX Runtime), MCP resources protocol.

**Estimate:** 5-7 days.

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
- Modify: `src/icm/memory_store.cpp::recall()` — accept `--semantic` flag.
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
- [ ] `npm test | icmg filter test` produces filtered streaming output.
- [ ] All existing 15 tests pass.
- [ ] New tests: `test_embedder_smoke`, `test_vec_search`, `test_agent_dryrun`.

---

## Rollback

Embedding is opt-in; `--semantic` default off. Agent requires explicit config. MCP resources additive to existing tools list.

---

## Risk Mitigation

| Risk | Mitigation |
|---|---|
| Python sidecar crash | Fall back to BM25; log warning |
| LLM agent cost spike | Hard token cap per session; dry-run mode |
| Embedding staleness | Embed-on-write hook; nightly re-embed task |
| MCP cache invalidation | Watch graph_nodes mtime; bust cache on scan |
