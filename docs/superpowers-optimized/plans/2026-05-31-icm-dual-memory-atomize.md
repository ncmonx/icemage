# ICM Dual-Memory + Atomize Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:subagent-driven-development (recommended) or superpowers-optimized:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a semantic memory layer of atomic facts ("atoms") derived asynchronously from raw stored memories, so recall hits single self-contained facts instead of large multi-fact blobs — without adding latency to `store` or `recall`.

**Architecture:** Dual layer. **Episodic** = existing `memory_nodes` (raw blobs, unchanged, source of truth). **Semantic** = new `memory_atoms` table, one row per atomic proposition, linked by `source_node_id`. Atomization happens **off the hot path**: `store()` enqueues the new node id into `memory_atom_queue`; a background worker (`icmg memory atomize run`, also driven by the existing `compact-bg`) drains the queue, splits content into atoms (heuristic by default, opt-in warm-pool LLM), dedups within zone, and inserts atoms with precomputed embeddings. Recall is unchanged by default; an opt-in hybrid path (`--atoms`) matches against atom FTS for precision and clusters atoms back to their source node at pack time. RAM RecallCache (v1.77) and FTS5 recall stay sub-10 ms.

**Tech Stack:** C++23, SQLite (FTS5 + WAL), MSVC 2026 build via `build.ps1`, existing `icmg_lib`, warm-pool LLM (`icmg::llm::tryWarmInfer`), embedded migrations (`core::Migrator`), mono ctest harness (`tests/test_main.hpp`).

**Assumptions:**
- Assumes atomization is **never** required to be synchronous — store returns before any atom exists. Will NOT work if a caller expects atoms immediately after `store()` returns (must poll/await worker).
- Assumes heuristic sentence/proposition split is "good enough" for default; LLM atomize is opt-in. Will NOT produce high-quality coreference-resolved atoms without `ICMG_ATOMIZE_LLM=1` + a warm model.
- Assumes the warm-pool model, when enabled, tolerates short prompts under load. Will NOT meet latency goals if LLM atomize runs synchronously on the store path (it must only run in the worker).
- Assumes `memory_nodes.id` is stable and never reused after soft-delete. Will NOT maintain atom→source integrity if ids are recycled.

---

## File Structure

| File | Responsibility |
|---|---|
| `migrations/global/0034_memory_atoms.sql` | NEW. `memory_atoms` + `memory_atom_queue` tables + FTS5 + `IF NOT EXISTS` guards. |
| `src/core/embedded_migrations.hpp` | MODIFY. Append 0034 C-string (build step / generator). |
| `src/imem/atom_split.hpp` | NEW. Header-only pure heuristic splitter `atomSplit(text)` → `vector<string>`. Testable, no I/O, no LLM. |
| `src/imem/atom_store.hpp` / `.cpp` | NEW. `AtomStore`: enqueue(node_id), drainQueue(max), insertAtom, recallAtoms(query), dedup-within-zone. Owns atom-table SQL. |
| `src/imem/memory_store.cpp` | MODIFY. `store()` calls `AtomStore::enqueue(id)` after successful insert (best-effort, never throws on hot path). |
| `src/imem/atom_llm.hpp` | NEW. Header-only `llmAtomize(text)` wrapper around `tryWarmInfer` with strict parse + heuristic fallback. |
| `src/cli/commands/atomize_cmd.cpp` | NEW. `icmg memory atomize run|status|stats` + opt-out `ICMG_ATOMIZE=0`. |
| `src/cli/commands/recall_cmd.cpp` | MODIFY. Add `--atoms` flag → hybrid atom-FTS recall + source clustering. |
| `tests/imem/test_atom_split.cpp` | NEW. Heuristic splitter unit tests. |
| `tests/imem/test_atom_store.cpp` | NEW. enqueue/drain/insert/dedup/recall tests (in-memory SQLite). |
| `tests/cli/test_atomize_cmd.cpp` | NEW. CLI worker + opt-out tests. |
| `CMakeLists.txt` | MODIFY. Register 3 new `add_icmg_test` targets. |

---

## Task 1: Schema — `memory_atoms` + `memory_atom_queue`

**Files:**
- Create: `migrations/global/0034_memory_atoms.sql`
- Modify: `src/core/embedded_migrations.hpp`
- Test: `tests/imem/test_atom_store.cpp`

**Does NOT cover:** embeddings column is `BLOB NULL` — atoms with no embedding fall back to BM25-only at recall (Task 7 handles null-embedding path). Does not cover cross-project atoms (scope column present but single-project default).

- [ ] **Step 1: Write failing test**

```cpp
// tests/imem/test_atom_store.cpp
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"

TEST("atom schema: memory_atoms + queue tables exist after migrate") {
    icmg::core::Db db(":memory:");
    icmg::core::Migrator::applyAll(db);   // applies embedded incl 0034
    // both tables must be queryable
    db.run("INSERT INTO memory_atoms(source_node_id,content,keywords,zone,created_at) "
           "VALUES(1,'fact one','k','default',100)");
    db.run("INSERT INTO memory_atom_queue(node_id,enqueued_at) VALUES(1,100)");
    auto n = db.queryInt("SELECT COUNT(*) FROM memory_atoms");
    ASSERT_EQ(n, 1);
    auto q = db.queryInt("SELECT COUNT(*) FROM memory_atom_queue");
    ASSERT_EQ(q, 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_store"`
Expected: FAIL — `no such table: memory_atoms`.

- [ ] **Step 3: Implement minimal change**

```sql
-- migrations/global/0034_memory_atoms.sql
-- v1.79.0 ICM dual-memory: semantic atom layer derived from memory_nodes.
CREATE TABLE IF NOT EXISTS memory_atoms (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    source_node_id INTEGER NOT NULL,
    content        TEXT    NOT NULL,
    keywords       TEXT    NOT NULL DEFAULT '',
    embedding      BLOB,                       -- nullable: BM25 fallback when null
    zone           TEXT    NOT NULL DEFAULT 'default',
    scope          TEXT    NOT NULL DEFAULT '',
    created_at     INTEGER NOT NULL DEFAULT 0,
    deleted_at     INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_atoms_source ON memory_atoms(source_node_id);
CREATE INDEX IF NOT EXISTS idx_atoms_zone   ON memory_atoms(zone);

CREATE VIRTUAL TABLE IF NOT EXISTS memory_atoms_fts USING fts5(
    content, keywords, content='memory_atoms', content_rowid='id'
);

-- atomize work queue: store() enqueues; worker drains.
CREATE TABLE IF NOT EXISTS memory_atom_queue (
    node_id     INTEGER PRIMARY KEY,           -- one pending entry per node
    enqueued_at INTEGER NOT NULL DEFAULT 0,
    attempts    INTEGER NOT NULL DEFAULT 0
);
```

Then append to `src/core/embedded_migrations.hpp` the `{34, R"SQL(...)SQL"}` entry mirroring the file content (follow the existing 0033 entry format exactly — same array, same raw-string delimiter convention).

- [ ] **Step 4: Run test to verify it passes**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_store"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add migrations/global/0034_memory_atoms.sql src/core/embedded_migrations.hpp tests/imem/test_atom_store.cpp CMakeLists.txt
git commit -m "v1.79 atom schema: memory_atoms + queue (migration 0034)"
```

---

## Task 2: Heuristic atom splitter (pure, no LLM)

**Files:**
- Create: `src/imem/atom_split.hpp`
- Test: `tests/imem/test_atom_split.cpp`

**Does NOT cover:** coreference resolution ("it"/"this" stay as-is — only LLM atomize in Task 6 resolves them). Does not split inside code fences or bullet sub-clauses beyond sentence boundaries.

- [ ] **Step 1: Write failing test**

```cpp
// tests/imem/test_atom_split.cpp
#include "../test_main.hpp"
#include "../../src/imem/atom_split.hpp"

TEST("atom_split: splits on sentence boundaries") {
    auto v = icmg::imem::atomSplit("Fix auth bug. Token check uses < not <=. Added test.");
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_EQ(v[0], std::string("Fix auth bug."));
    ASSERT_EQ(v[2], std::string("Added test."));
}

TEST("atom_split: splits bullet lines into atoms") {
    auto v = icmg::imem::atomSplit("- decision A chosen\n- decision B rejected\n- open item C");
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_EQ(v[1], std::string("decision B rejected"));
}

TEST("atom_split: drops empties + trims; single short text = one atom") {
    auto v = icmg::imem::atomSplit("   single fact   ");
    ASSERT_EQ((int)v.size(), 1);
    ASSERT_EQ(v[0], std::string("single fact"));
}

TEST("atom_split: does not split inside code fence") {
    auto v = icmg::imem::atomSplit("Use this. ```a. b. c.``` Done.");
    // code fence kept as one atom; 3 atoms total: "Use this." / fence / "Done."
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_TRUE(v[1].find("```") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_split"`
Expected: FAIL — `atom_split.hpp` not found / no `atomSplit`.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/imem/atom_split.hpp
#pragma once
#include <string>
#include <vector>
#include <cctype>

namespace icmg::imem {

// Heuristic atomizer: splits text into atomic propositions WITHOUT an LLM.
// Rules (in order):
//   1. Fenced code blocks (```...```) are emitted as a single atom, never split.
//   2. Bullet lines (leading "- " / "* ") each become one atom (marker stripped).
//   3. Remaining prose splits on sentence terminators . ! ? followed by space/EOL.
//   4. Trim whitespace; drop empties; collapse atoms shorter than 2 chars.
inline std::vector<std::string> atomSplit(const std::string& text) {
    std::vector<std::string> out;
    auto push = [&](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return;
        size_t b = s.find_last_not_of(" \t\r\n");
        s = s.substr(a, b - a + 1);
        // strip leading bullet marker
        if (s.size() >= 2 && (s[0] == '-' || s[0] == '*') && s[1] == ' ')
            s = s.substr(2);
        if (s.size() >= 2) out.push_back(std::move(s));
    };

    size_t i = 0, n = text.size();
    std::string cur;
    while (i < n) {
        // code fence
        if (text.compare(i, 3, "```") == 0) {
            push(cur); cur.clear();
            size_t end = text.find("```", i + 3);
            size_t close = (end == std::string::npos) ? n : end + 3;
            out.push_back(text.substr(i, close - i));   // emit fence verbatim
            i = close;
            continue;
        }
        char c = text[i];
        // newline = bullet/line boundary
        if (c == '\n') { push(cur); cur.clear(); ++i; continue; }
        cur.push_back(c);
        // sentence terminator followed by space or EOL
        if ((c == '.' || c == '!' || c == '?') &&
            (i + 1 >= n || text[i + 1] == ' ' || text[i + 1] == '\n')) {
            push(cur); cur.clear();
        }
        ++i;
    }
    push(cur);
    return out;
}

} // namespace icmg::imem
```

- [ ] **Step 4: Run test to verify it passes**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_split"`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add src/imem/atom_split.hpp tests/imem/test_atom_split.cpp CMakeLists.txt
git commit -m "v1.79 atom_split: pure heuristic proposition splitter"
```

---

## Task 3: AtomStore — enqueue + drain + insert

**Files:**
- Create: `src/imem/atom_store.hpp`, `src/imem/atom_store.cpp`
- Test: `tests/imem/test_atom_store.cpp` (extend)

**Does NOT cover:** embeddings (Task 5). Inserts atoms with `embedding=NULL`. Does not run LLM (Task 6). Drain uses heuristic `atomSplit` only here.

- [ ] **Step 1: Write failing test**

```cpp
// append to tests/imem/test_atom_store.cpp
#include "../../src/imem/atom_store.hpp"

TEST("atom_store: enqueue then drain inserts atoms + clears queue") {
    icmg::core::Db db(":memory:");
    icmg::core::Migrator::applyAll(db);
    db.run("INSERT INTO memory_atoms(source_node_id,content,zone,created_at) VALUES(0,'seed','default',1)"); // warm fts trigger path
    icmg::imem::AtomStore as(db);
    as.enqueue(42, "Fix bug. Added test. Shipped fix.", "default", 1000);
    int processed = as.drainQueue(10);
    ASSERT_EQ(processed, 1);                       // 1 node processed
    auto atoms = db.queryInt("SELECT COUNT(*) FROM memory_atoms WHERE source_node_id=42");
    ASSERT_EQ(atoms, 3);                           // 3 propositions
    auto pending = db.queryInt("SELECT COUNT(*) FROM memory_atom_queue");
    ASSERT_EQ(pending, 0);                         // queue drained
}

TEST("atom_store: dedup within zone skips identical atom from same source") {
    icmg::core::Db db(":memory:");
    icmg::core::Migrator::applyAll(db);
    icmg::imem::AtomStore as(db);
    as.enqueue(7, "Same fact. Same fact.", "default", 1);
    as.drainQueue(10);
    auto atoms = db.queryInt("SELECT COUNT(*) FROM memory_atoms WHERE source_node_id=7");
    ASSERT_EQ(atoms, 1);                           // duplicate proposition collapsed
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_store"`
Expected: FAIL — `atom_store.hpp` not found.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/imem/atom_store.hpp
#pragma once
#include "../core/db.hpp"
#include <string>

namespace icmg::imem {

class AtomStore {
public:
    explicit AtomStore(core::Db& db) : db_(db) {}

    // Hot-path safe: single INSERT OR REPLACE into queue. Never splits here.
    void enqueue(int64_t node_id, const std::string& content,
                 const std::string& zone, int64_t now_sec);

    // Worker path: pop up to `max` queued nodes, split via atomSplit,
    // dedup within (source_node_id), insert atoms (embedding NULL here).
    // Returns number of nodes processed.
    int drainQueue(int max);

    // BM25 recall over atom FTS. Returns source_node_ids ranked, deduped.
    std::vector<int64_t> recallAtomSources(const std::string& query, int limit);

private:
    core::Db& db_;
};

} // namespace icmg::imem
```

```cpp
// src/imem/atom_store.cpp
#include "atom_store.hpp"
#include "atom_split.hpp"
#include <unordered_set>

namespace icmg::imem {

void AtomStore::enqueue(int64_t node_id, const std::string& content,
                        const std::string& zone, int64_t now_sec) {
    // content/zone passed for future direct-split; here we only record node_id.
    // INSERT OR REPLACE keeps one pending row per node (idempotent re-enqueue).
    auto st = db_.prepare("INSERT OR REPLACE INTO memory_atom_queue(node_id,enqueued_at,attempts) "
                          "VALUES(?, ?, COALESCE((SELECT attempts FROM memory_atom_queue WHERE node_id=?),0))");
    st.bind(1, node_id); st.bind(2, now_sec); st.bind(3, node_id);
    st.step();
}

int AtomStore::drainQueue(int max) {
    // pull pending node ids
    std::vector<int64_t> ids;
    {
        auto st = db_.prepare("SELECT node_id FROM memory_atom_queue ORDER BY enqueued_at LIMIT ?");
        st.bind(1, max);
        while (st.step()) ids.push_back(st.columnInt64(0));
    }
    int processed = 0;
    for (int64_t id : ids) {
        // fetch source content + zone from memory_nodes
        std::string content, zone = "default";
        {
            auto q = db_.prepare("SELECT content, zone FROM memory_nodes WHERE id=? AND deleted_at=0");
            q.bind(1, id);
            if (q.step()) { content = q.columnText(0); zone = q.columnText(1); }
        }
        if (!content.empty()) {
            auto atoms = atomSplit(content);
            std::unordered_set<std::string> seen;
            for (auto& a : atoms) {
                if (!seen.insert(a).second) continue;          // dedup within source
                auto ins = db_.prepare("INSERT INTO memory_atoms"
                    "(source_node_id,content,keywords,zone,created_at) VALUES(?,?,?,?,?)");
                ins.bind(1, id); ins.bind(2, a); ins.bind(3, std::string());
                ins.bind(4, zone); ins.bind(5, (int64_t)0);
                ins.step();
                int64_t rid = db_.lastInsertRowid();
                auto fts = db_.prepare("INSERT INTO memory_atoms_fts(rowid,content,keywords) VALUES(?,?,'')");
                fts.bind(1, rid); fts.bind(2, a); fts.step();
            }
        }
        auto del = db_.prepare("DELETE FROM memory_atom_queue WHERE node_id=?");
        del.bind(1, id); del.step();
        ++processed;
    }
    return processed;
}

std::vector<int64_t> AtomStore::recallAtomSources(const std::string& query, int limit) {
    std::vector<int64_t> out;
    std::unordered_set<int64_t> seen;
    auto st = db_.prepare(
        "SELECT a.source_node_id FROM memory_atoms_fts f "
        "JOIN memory_atoms a ON a.id=f.rowid "
        "WHERE memory_atoms_fts MATCH ? AND a.deleted_at=0 "
        "ORDER BY rank LIMIT ?");
    st.bind(1, query); st.bind(2, limit * 4);
    while (st.step() && (int)out.size() < limit) {
        int64_t sid = st.columnInt64(0);
        if (seen.insert(sid).second) out.push_back(sid);
    }
    return out;
}

} // namespace icmg::imem
```

> NOTE for implementer: match the actual `core::Db` prepared-statement API (`prepare/bind/step/columnInt64/columnText/lastInsertRowid/queryInt`). If method names differ, adapt — verify against `src/core/db.hpp` before writing. Do NOT invent methods.

- [ ] **Step 4: Run test to verify it passes**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_store"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/imem/atom_store.hpp src/imem/atom_store.cpp tests/imem/test_atom_store.cpp
git commit -m "v1.79 AtomStore: enqueue + heuristic drain + within-source dedup"
```

---

## Task 4: Wire `store()` to enqueue (hot-path safe)

**Files:**
- Modify: `src/imem/memory_store.cpp`
- Test: `tests/imem/test_atom_store.cpp` (extend)

**Does NOT cover:** synchronous atomization. `store()` ONLY enqueues; atoms appear after a worker run. Enqueue failure must NOT fail `store()` (best-effort, swallowed).

- [ ] **Step 1: Write failing test**

```cpp
// append to tests/imem/test_atom_store.cpp
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"

TEST("store: enqueues node for atomization but does not block") {
    icmg::core::Db db(":memory:");
    icmg::core::Migrator::applyAll(db);
    icmg::imem::MemoryStore ms(db);
    icmg::imem::MemoryNode n;
    n.topic = "t"; n.content = "Fact one. Fact two."; n.zone = "default";
    int64_t id = ms.store(n, true);
    ASSERT_TRUE(id > 0);
    // queue has exactly this node, NO atoms yet (worker not run)
    auto pending = db.queryInt("SELECT COUNT(*) FROM memory_atom_queue WHERE node_id=" + std::to_string(id));
    ASSERT_EQ(pending, 1);
    auto atoms = db.queryInt("SELECT COUNT(*) FROM memory_atoms");
    ASSERT_EQ(atoms, 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_store"`
Expected: FAIL — `pending == 0` (store does not enqueue yet).

- [ ] **Step 3: Implement minimal change**

In `src/imem/memory_store.cpp`, at the end of `store()` after the successful insert obtains `new_id`, add (respect `ICMG_ATOMIZE=0` opt-out):

```cpp
// v1.79 ICM dual-memory: enqueue for async atomization (best-effort, never throws).
if (const char* off = std::getenv("ICMG_ATOMIZE"); !(off && off[0] == '0')) {
    try {
        AtomStore(db_).enqueue(new_id, node.content, node.zone, std::time(nullptr));
    } catch (...) { /* hot-path safe: atomization is derived, non-critical */ }
}
```

Add `#include "atom_store.hpp"` and `#include <ctime>`/`#include <cstdlib>` to the top of `memory_store.cpp` if not present. Verify the actual local variable name for the inserted id (`new_id` here is illustrative — match the real one in `store()`).

- [ ] **Step 4: Run test to verify it passes**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_store"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/imem/memory_store.cpp tests/imem/test_atom_store.cpp
git commit -m "v1.79 store(): best-effort enqueue for async atomization (ICMG_ATOMIZE opt-out)"
```

---

## Task 5: Embeddings for atoms (precompute at drain, ONNX-gated)

**Files:**
- Modify: `src/imem/atom_store.cpp`
- Test: `tests/imem/test_atom_store.cpp` (extend)

**Does NOT cover:** the ONNX-absent path — when no embedder is compiled/available, `embedding` stays NULL and recall uses BM25 only. Test asserts the NULL-fallback path (no ONNX in test build).

- [ ] **Step 1: Write failing test**

```cpp
// append to tests/imem/test_atom_store.cpp
TEST("atom_store: drain leaves embedding NULL when no embedder (BM25 fallback)") {
    icmg::core::Db db(":memory:");
    icmg::core::Migrator::applyAll(db);
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) "
           "VALUES('t','Alpha fact. Beta fact.','','default',1)");
    int64_t nid = db.lastInsertRowid();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, "Alpha fact. Beta fact.", "default", 1);
    as.drainQueue(10);
    // embeddings NULL in a test build with no ONNX runtime
    auto nullCount = db.queryInt("SELECT COUNT(*) FROM memory_atoms WHERE embedding IS NULL AND source_node_id=" + std::to_string(nid));
    ASSERT_EQ(nullCount, 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_store"`
Expected: PASS already if drain inserts NULL embedding — if so, this test **locks** the fallback contract. If embeddings were wired wrong it FAILs. (Guard test.)

- [ ] **Step 3: Implement minimal change**

In `drainQueue`, after inserting the atom row, attempt embedding only when the embedder backend is available (mirror the `gist_cmd`/`recallSemantic` ONNX-gate pattern — look up the existing `embed::Embedder`/factory used by `recallSemantic` in `memory_store.cpp` and reuse it). Pseudocode contract:

```cpp
// after `int64_t rid = db_.lastInsertRowid();`
if (auto* emb = /* existing embedder factory, nullptr when ONNX absent */; emb) {
    auto vec = emb->embed(a);                 // vector<float>
    if (!vec.empty()) {
        auto up = db_.prepare("UPDATE memory_atoms SET embedding=? WHERE id=?");
        up.bindBlob(1, vec.data(), vec.size() * sizeof(float));
        up.bind(2, rid); up.step();
    }
}
```

Do NOT add a hard ONNX dependency — gate exactly as `recallSemantic` does. When absent, leave NULL.

- [ ] **Step 4: Run test to verify it passes**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_store"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/imem/atom_store.cpp tests/imem/test_atom_store.cpp
git commit -m "v1.79 atoms: precompute embeddings at drain (ONNX-gated, NULL fallback)"
```

---

## Task 6: Opt-in LLM atomize (warm-pool, worker-only)

**Files:**
- Create: `src/imem/atom_llm.hpp`
- Modify: `src/imem/atom_store.cpp`
- Test: `tests/imem/test_atom_split.cpp` (extend — parse logic only, deterministic)

**Does NOT cover:** running the model in tests (non-deterministic). Tests only the **parse** of LLM output + the heuristic fallback. The model call is smoke-only, gated `ICMG_ATOMIZE_LLM=1`, and runs ONLY in `drainQueue` (never on store).

- [ ] **Step 1: Write failing test**

```cpp
// append to tests/imem/test_atom_split.cpp
#include "../../src/imem/atom_llm.hpp"

TEST("atom_llm: parses one-fact-per-line model output") {
    auto v = icmg::imem::parseLlmAtoms("- user fixed auth bug\n- token check off-by-one\n\n- added regression test\n");
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_EQ(v[1], std::string("token check off-by-one"));
}

TEST("atom_llm: empty/garbage model output falls back to heuristic split") {
    auto v = icmg::imem::llmAtomizeOrFallback("Fact A. Fact B.", /*model_output=*/"");
    ASSERT_EQ((int)v.size(), 2);          // heuristic fallback
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_split"`
Expected: FAIL — `atom_llm.hpp` not found.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/imem/atom_llm.hpp
#pragma once
#include "atom_split.hpp"
#include <string>
#include <vector>

namespace icmg::imem {

// Parse "- one fact per line" model output into atoms. Trims, drops empties + markers.
inline std::vector<std::string> parseLlmAtoms(const std::string& out) {
    std::vector<std::string> v;
    size_t i = 0, n = out.size();
    while (i < n) {
        size_t e = out.find('\n', i);
        if (e == std::string::npos) e = n;
        std::string line = out.substr(i, e - i);
        size_t a = line.find_first_not_of(" \t\r-*");
        if (a != std::string::npos) {
            size_t b = line.find_last_not_of(" \t\r");
            std::string s = line.substr(a, b - a + 1);
            if (s.size() >= 2) v.push_back(std::move(s));
        }
        i = e + 1;
    }
    return v;
}

// If model_output parses to >=1 atom, use it; else heuristic fallback.
inline std::vector<std::string> llmAtomizeOrFallback(const std::string& src,
                                                     const std::string& model_output) {
    auto v = parseLlmAtoms(model_output);
    if (!v.empty()) return v;
    return atomSplit(src);
}

} // namespace icmg::imem
```

In `atom_store.cpp::drainQueue`, replace the `atomSplit(content)` call with:

```cpp
std::vector<std::string> atoms;
if (const char* ll = std::getenv("ICMG_ATOMIZE_LLM"); ll && ll[0] == '1') {
    std::string prompt = "Split into atomic facts, one per line, each self-contained:\n" + content;
    std::string out;
    if (icmg::llm::tryWarmInfer(prompt, out))   // returns false when no warm model
        atoms = llmAtomizeOrFallback(content, out);
    else
        atoms = atomSplit(content);
} else {
    atoms = atomSplit(content);
}
```

Add includes `#include "atom_llm.hpp"`, `#include "../llm/warm_client.hpp"`, `#include <cstdlib>`. Verify `tryWarmInfer` signature against `src/llm/warm_client.hpp` (adapt if it returns differently).

- [ ] **Step 4: Run test to verify it passes**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atom_split"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/imem/atom_llm.hpp src/imem/atom_store.cpp tests/imem/test_atom_split.cpp
git commit -m "v1.79 atom_llm: opt-in warm-pool LLM atomize (worker-only) + heuristic fallback"
```

---

## Task 7: `icmg memory atomize` CLI worker

**Files:**
- Create: `src/cli/commands/atomize_cmd.cpp`
- Test: `tests/cli/test_atomize_cmd.cpp`

**Does NOT cover:** auto-scheduling (compact-bg wiring is Task 8). `run` drains synchronously when invoked; `--max N` caps work per call. `ICMG_ATOMIZE=0` makes `run` a no-op.

- [ ] **Step 1: Write failing test**

```cpp
// tests/cli/test_atomize_cmd.cpp
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/imem/atom_store.hpp"

TEST("atomize_cmd: drain count matches pending queue") {
    icmg::core::Db db(":memory:");
    icmg::core::Migrator::applyAll(db);
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) VALUES('t','A. B. C.','','default',1)");
    int64_t nid = db.lastInsertRowid();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, "A. B. C.", "default", 1);
    int processed = as.drainQueue(100);
    ASSERT_EQ(processed, 1);
    auto atoms = db.queryInt("SELECT COUNT(*) FROM memory_atoms WHERE source_node_id=" + std::to_string(nid));
    ASSERT_EQ(atoms, 3);
}
```

> Rationale: the CLI command is a thin shell over `AtomStore::drainQueue` (already tested in Task 3). This test locks the worker contract the command depends on; the command body itself is I/O glue verified by smoke-run in Step 4.

- [ ] **Step 2: Run test to verify it fails (or locks contract)**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atomize_cmd"`
Expected: FAIL until the test file + CMake target exist; then PASS.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/cli/commands/atomize_cmd.cpp
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/atom_store.hpp"
#include <iostream>
#include <cstdlib>

namespace icmg::cli {

class AtomizeCommand : public BaseCommand {
public:
    std::string name() const override { return "atomize"; }
    std::string description() const override { return "Drain the memory atomization queue (semantic atom layer)"; }

    int run(const std::vector<std::string>& args) override {
        if (const char* off = std::getenv("ICMG_ATOMIZE"); off && off[0] == '0') {
            std::cout << "atomize: disabled (ICMG_ATOMIZE=0)\n"; return 0;
        }
        core::Db db(core::Config::instance().projectDbPath());
        imem::AtomStore as(db);
        std::string sub = args.empty() ? "run" : args[0];
        if (sub == "status" || sub == "stats") {
            auto pending = db.queryInt("SELECT COUNT(*) FROM memory_atom_queue");
            auto atoms   = db.queryInt("SELECT COUNT(*) FROM memory_atoms WHERE deleted_at=0");
            std::cout << "atoms: " << atoms << "  pending: " << pending << "\n";
            return 0;
        }
        int max = 256;
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == "--max") max = std::atoi(args[i+1].c_str());
        int n = as.drainQueue(max);
        std::cout << "atomize: processed " << n << " node(s)\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("atomize", AtomizeCommand);

} // namespace icmg::cli
```

> NOTE: `memory atomize` routing — if `memory` is a parent dispatcher, register under it following the existing `memory cache` sub-command pattern; otherwise top-level `atomize` is fine. Verify how `icmg memory cache` is wired before choosing.

- [ ] **Step 4: Run test + smoke-run**

Run: `powershell -File build.ps1 -Target both -RunTests -TestFilter "atomize_cmd"`
Then smoke: `C:\icmg-build\build-msvc-full\icmg.exe atomize status`
Expected: test PASS; smoke prints `atoms: N  pending: M`.

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/atomize_cmd.cpp tests/cli/test_atomize_cmd.cpp CMakeLists.txt
git commit -m "v1.79 icmg atomize: CLI worker (run/status) + ICMG_ATOMIZE opt-out"
```

---

## Task 8: Auto-drive worker from background (compact-bg / service tick)

**Files:**
- Modify: `src/cli/commands/atomize_cmd.cpp` (no-op if already covered) and the background worker entrypoint (`compact-bg` command or `core/service_loop.cpp` tick).
- Test: none new (integration; covered by Task 3/7 unit tests + smoke).

**Does NOT cover:** real-time atomization. Worker fires on the existing service tick cadence (minutes), NOT per-store. If the service/daemon is off, atoms only appear on explicit `icmg atomize run`.

- [ ] **Step 1: Locate the background tick**

Run: `icmg graph symbol compactBg` (and inspect `src/core/service_loop.cpp`).
Identify where periodic project maintenance runs.

- [ ] **Step 2: Add a bounded drain call to the tick**

In the service tick (or `compact-bg`), add a best-effort, capped drain so the queue self-empties:

```cpp
// background maintenance tick — bounded so it never hogs the worker
try {
    icmg::imem::AtomStore(db).drainQueue(64);   // small batch per tick
} catch (...) { /* derived data; ignore */ }
```

Gate with the same `ICMG_ATOMIZE=0` opt-out.

- [ ] **Step 3: Build + smoke**

Run: `powershell -File build.ps1 -Target icmg`
Smoke: store a node, run the service tick once (or `icmg atomize run`), confirm `icmg atomize status` shows `pending: 0`.
Expected: queue drains.

- [ ] **Step 4: Commit**

```bash
git add src/core/service_loop.cpp src/cli/commands/atomize_cmd.cpp
git commit -m "v1.79 atomize: bounded auto-drain on background tick (ICMG_ATOMIZE opt-out)"
```

---

## Task 9: Hybrid `recall --atoms` (opt-in, source-clustered)

**Files:**
- Modify: `src/cli/commands/recall_cmd.cpp`
- Test: `tests/cli/test_atomize_cmd.cpp` (extend — recallAtomSources tested at store layer in Task 3; here lock the flag plumbing)

**Does NOT cover:** changing default recall. Without `--atoms`, recall is byte-for-byte unchanged (zero latency/behavior risk). `--atoms` matches atom FTS, then returns the **source** `memory_nodes` (clustered), so output shape is identical to normal recall.

- [ ] **Step 1: Write failing test**

```cpp
// append to tests/cli/test_atomize_cmd.cpp
TEST("recall_atoms: atom match maps back to source node") {
    icmg::core::Db db(":memory:");
    icmg::core::Migrator::applyAll(db);
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) "
           "VALUES('t','The linker error came from a missing symbol. Rebuilt clean.','','default',1)");
    int64_t nid = db.lastInsertRowid();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, "The linker error came from a missing symbol. Rebuilt clean.", "default", 1);
    as.drainQueue(10);
    auto sources = as.recallAtomSources("linker missing symbol", 5);
    ASSERT_TRUE(!sources.empty());
    ASSERT_EQ(sources[0], nid);
}
```

- [ ] **Step 2: Run test to verify it fails (or locks contract)**

Run: `powershell -File build.ps1 -Target test -RunTests -TestFilter "atomize_cmd"`
Expected: PASS once `recallAtomSources` (Task 3) is built — locks the recall mapping.

- [ ] **Step 3: Implement minimal change**

In `recall_cmd.cpp`, detect `--atoms` flag. When present:

```cpp
// when --atoms passed: match atom FTS, fetch source nodes in rank order
if (hasFlag(args, "--atoms")) {
    imem::AtomStore as(db);
    auto srcIds = as.recallAtomSources(query, limit);
    std::vector<imem::MemoryNode> nodes;
    for (int64_t id : srcIds) {
        auto n = store.getById(id);          // existing accessor; verify name
        if (n.id != 0) nodes.push_back(n);
    }
    // print using the SAME formatter as normal recall
    printRecall(nodes);
    return 0;
}
```

Verify `getById`/equivalent exists on `MemoryStore`; if not, add a small `getById(int64_t)` accessor (one prepared SELECT) in a sub-task before this. Reuse the existing recall output formatter so `--atoms` output is indistinguishable in shape.

- [ ] **Step 4: Run test + smoke**

Run: `powershell -File build.ps1 -Target both -RunTests -TestFilter "atomize_cmd"`
Smoke: `icmg recall "linker symbol" --atoms`
Expected: PASS; smoke returns the source memory node(s).

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/recall_cmd.cpp tests/cli/test_atomize_cmd.cpp
git commit -m "v1.79 recall --atoms: opt-in atom-FTS hybrid, source-clustered output"
```

---

## Task 10: Version bump + 5-sync + ship

**Files:**
- Modify: `CMakeLists.txt`, `src/core/version.hpp`, `src/icmg.rc`

- [ ] **Step 1: Bump version 1.78.4 → 1.79.0** in all three files (`project(icmg VERSION 1.79.0 ...)`, `ICMG_VERSION = "1.79.0"`, rc `1,79,0,0` + `"1.79.0"`).

- [ ] **Step 2: Full build + ctest**

Run: `powershell -File build.ps1 -Target both -RunTests`
Expected: mono ctest PASS; record new atomic count (was 1187 + new atom tests ≈ +13).

- [ ] **Step 3: 5-sync (icmg)**

```bash
icmg graph update
icmg store --topic decisions-icm-dual-memory "v1.79.0 ICM dual-memory: episodic blob + semantic atom layer; atomize async (store enqueues, worker drains heuristic default / opt-in warm-LLM); recall --atoms opt-in source-clustered; ICMG_ATOMIZE/ICMG_ATOMIZE_LLM env gates; migration 0034."
icmg zone add "src/imem/atom_*.{hpp,cpp}" --zone memory
icmg wflog save --goal "v1.79.0 ICM dual-memory + atomize" --decisions "..." --open "ship"
icmg verify --command "build.ps1 -Target both -RunTests"
```

- [ ] **Step 4: Ship** — follow CLAUDE.md release: stage exe + 13 DLLs (reuse proven bundle) → zip → sha256 → docs PR (What's-new memoir prepend v1.79.0, drop oldest; headline numbers + atom row; ctest count) → merge → tag on docs-commit → release create → upload → ctest gist + repo About sync.

- [ ] **Step 5: Commit version bump**

```bash
git add CMakeLists.txt src/core/version.hpp src/icmg.rc
git commit -m "v1.79.0: ICM dual-memory + atomize (bump 1.78.4->1.79.0)"
```

---

## Latency contract (verify before ship)

| Path | Guarantee | Mechanism |
|---|---|---|
| `store()` | +1 INSERT only (sub-ms) | enqueue, never split inline |
| `recall` (default) | unchanged | `--atoms` opt-in; default path untouched |
| `recall --atoms` | FTS5 sub-10 ms | atom FTS + RAM cache, no LLM |
| atomize worker | off hot path | background tick / explicit `run`, bounded batch |
| LLM atomize | worker-only, opt-in | `ICMG_ATOMIZE_LLM=1`, never on store |
