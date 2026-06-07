# Memory-Graph Architecture — Design Reasoning (#memory-graph)

> 2026-06-07. Reasoning in response to kak Cahyo's question (relayed via luna over wire):
> "from these 4 memory-tool architectures, what's the right *pattern* for the vessel?"
> Source research (luna): agentmem, MemoryGraph, GraphMemory, GraphMem-MCP.

## The key realization the research missed

icmg **already has a graph engine** — `GraphStore`: a SQLite-backed DAG of typed nodes/edges with
BFS closure, SCC (Tarjan), cycle detection, case-mixed dedup. But it is pointed at **code only**
(file/symbol nodes, import/call edges). The gap is not "no graph" — it is **"memory is not on the graph."**

Decision lens for every question below: **maximize reuse of the existing engine, minimize new surface.**

## Q1 — Which architecture is the best *pattern* reference?

**MemoryGraph** (typed relations + lineage) — because typed edges ride the existing `GraphStore`
(highest leverage, lowest cost). Not a wholesale copy:

| Tool | Verdict |
|---|---|
| MemoryGraph (7 typed relations, bi-temporal) | Borrow the **relation model** (typed edges) |
| agentmem (5 tiers, entity extraction) | Borrow the **tier model** (orthogonal axis) |
| GraphMemory (6 graphs, 70 MCP tools) | Reject — over-engineered, surface bloat we'd never use |
| GraphMem-MCP (per-project SQLite graph) | Closest to what we already have; confirms direction |

→ Borrow **relations from MemoryGraph + tiers from agentmem** (two orthogonal axes), not one tool whole.

## Q2 — 6 interconnected graphs, relevant to our dual-layer?

**No — keep 2.** Our split (project = technical/shared, persona = rasa/identity/local-only) is a
**trust boundary** — it must stay 2. GraphMemory's 6 graphs (docs/code/notes/tasks/skills/files) are
**domain** partitions within one trust zone — that is what **zones** already do (zone-scoped recall).

→ 2 DBs (privacy boundary) + zones for domain partitioning. **Multiply zones (cheap, exists), not
graphs.** Six separate graphs would re-implement zones as separate stores and risk blurring the
persona privacy line — the one line that must never blur.

## Q3 — agentmem "working" tier (auto-expire) vs our decay?

**Different concepts; both worth having.**
- **decay** = relevance fades gradually (score drops, node stays). icmg has `ageDecay` (90d half-life).
- **working-expire** = hard TTL tied to session lifetime (node deleted when session ends).

icmg already has `expires_at` (`--ttl`). A "working" tier = `expires_at = end-of-session`, automatic.
A **new tier riding the existing TTL mechanism**, complementary to decay. `quick:` captures are the
natural working-tier residents.

## Q4 — MemoryGraph bi-temporal (valid_from + superseded_at) — worth it?

**Mostly no.** Bi-temporal tracks *when a fact was true* AND *when we knew it* — powerful for
audit / time-travel ("what did we believe at time X"). Our domain wants the **best current context**,
not historical belief-state reconstruction. icmg already has `created_at` + supersede-by-keyword +
soft-delete (`deleted_at`), which loosely covers "superseded_at."

→ Full bi-temporal = 4 timestamps/node + query complexity for a query we won't run = **YAGNI**.
decay + recall + supersede is enough. At most add a single clean `superseded_at` for "this replaced
that" lineage; skip `valid_from`/`valid_to`.

## Synthesis — highest-leverage move

**Promote memoir/memory to first-class graph nodes with TYPED edges**, reusing `GraphStore` (the
engine exists → cheap). Then:
1. **Typed memory edges** (causal / solution / context / learning / supersedes) on memoir nodes.
2. **Thin tier layer** over existing `importance` + `expires_at` (add `working` = session-TTL;
   `procedural` ≈ our rules/anti-patterns).
3. **Entity extraction rule-based** (@mention / URL / IP / env via regex, zero-LLM) — extends Layer-0.

Principles held throughout: **reuse > rebuild · trust-boundary stays 2 · YAGNI on audit features
we won't query.**

## Next
- Spec the typed-memory-graph (move #1) — schema for memory-edge table (or reuse graph_edges with a
  memory-node kind), edge types enum, recall integration (BFS over typed edges).
- Backlog: tiers, entity-extraction Layer-0 enrichment, recall --last-session.
