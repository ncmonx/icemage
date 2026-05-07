# Phase 02: ICM Memory + BM25 Scorer

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Implementasi ICM memory store dengan BM25 scoring + recency decay + importance multiplier.
**Architecture:** MemoryStore class untuk CRUD, Scorer class untuk ranking. Auto-expand abbreviations via HookBus PRE_RECALL.
**Tech Stack:** C++17, SQLite (via core/db.hpp dari Phase 01)
**Assumptions:** Phase 01 selesai — db.hpp, config.hpp, hook_bus.hpp tersedia.

---

### Task 1: MemoryNode struct + MemoryStore CRUD

**Files:**
- Create: `src/imem/memory_node.hpp`
- Create: `src/imem/memory_store.hpp`
- Create: `src/imem/memory_store.cpp`

**memory_node.hpp:**
```cpp
struct MemoryNode {
    int64_t id = 0;
    std::string topic;
    std::string content;
    std::string keywords;   // comma-separated
    int importance = 1;     // 0=low 1=med 2=high 3=critical
    int frequency = 1;
    int64_t last_used = 0;
    int64_t created_at = 0;
    double score = 0.0;     // computed at query time
};
```

**memory_store.hpp methods:**
```cpp
class MemoryStore {
public:
    explicit MemoryStore(core::Db& db);
    int64_t store(const MemoryNode& node);
    bool update(int64_t id, const std::string& content);
    bool remove(int64_t id);
    std::vector<MemoryNode> recall(const std::string& query, int limit = 10);
    std::vector<MemoryNode> recallByTopic(const std::string& topic, int limit = 10);
    void bumpFrequency(int64_t id);
    std::vector<MemoryNode> all() const;
};
```

**Step 3: Verify store + retrieve**
```bash
./build/icmg store test "hello world" && ./build/icmg recall "hello"
```
Expected: node muncul di hasil recall.

---

### Task 2: BM25 Scorer

**Files:**
- Create: `src/imem/scorer.hpp`
- Create: `src/imem/scorer.cpp`

**Formula:**
```
score = BM25(query, doc) x recency_decay x freq_score x importance_mult

BM25(q, d) = sum[ IDF(qi) * (tf * (k1+1)) / (tf + k1*(1-b+b*|d|/avgdl)) ]
k1 = 1.5, b = 0.75
IDF = log((N+1)/(df+1) + 1)   // smoothed

recency_decay  = exp(-0.01 * hours_since_last_used)
freq_score     = log(1 + frequency)
importance_mult = {0:0.5, 1:1.0, 2:1.5, 3:2.0}
```

**scorer.hpp:**
```cpp
class Scorer {
public:
    void fit(const std::vector<MemoryNode>& corpus);
    double score(const std::string& query, const MemoryNode& node) const;
    std::vector<MemoryNode> rank(const std::string& query,
                                  std::vector<MemoryNode> nodes,
                                  int limit = 10) const;
private:
    double bm25(const std::string& query, const std::string& doc) const;
    double recencyDecay(int64_t last_used) const;
    std::unordered_map<std::string, int> df_;  // document frequency
    int N_ = 0;                                 // total docs
    double avgdl_ = 0.0;
};
```

**Step 3: Verify scoring**
Store 5+ nodes dengan topics berbeda. Query sesuatu — pastikan node paling relevan muncul di atas.

---

### Task 3: CLI commands: store + recall + forget + update

**Files:**
- Create: `src/cli/commands/store_cmd.cpp`
- Create: `src/cli/commands/recall_cmd.cpp`

**store usage:**
```
icmg store <topic> <content> [--importance low|med|high|crit] [--kw k1,k2]
```

**recall usage:**
```
icmg recall <query> [--limit N] [--topic X] [--json]
```

**recall output format (default):**
```
[89.2] decisions-project
  "Decided to use SQLite WAL mode for concurrent access"
  Keywords: sqlite, database, concurrency
  Used: 3x, last: 2h ago

[71.5] preferences
  "User prefers snake_case for all identifiers"
  ...
```

**--json output:**
```json
[{"id":1,"topic":"decisions-project","content":"...","score":89.2}]
```

**Step 3: Verify**
```bash
./build/icmg store preferences "caveman mode aktif"
./build/icmg recall "mode" --limit 5
./build/icmg recall "mode" --json
```
Expected: output terformat dengan score.

---

### Task 4: Hook: PRE_STORE auto-detect abbreviation pattern

**Files:**
- Create: `src/imem/hooks/store_hooks.cpp`

Detect pattern di content saat store:
- `bkm=bukti kas masuk`
- `bkm:bukti kas masuk`
- `(bkm) bukti kas masuk`

Jika terdeteksi → auto-insert ke tabel abbreviations (jika Phase 09 belum ada, skip silently).

---

### Task 5: Final verify + commit

**Step 1:**
```bash
cmake --build build
./build/icmg store test-topic "ini adalah test node untuk scoring" --importance high --kw test,scoring
./build/icmg store test-topic2 "node kedua dengan konten berbeda" --importance low
./build/icmg recall "scoring node"
./build/icmg recall "scoring node" --json
./build/icmg recall "node" --limit 1
```
Expected: hasil terurut by score, format benar.

**Step 2: Commit**
```bash
git add src/imem/ src/cli/commands/store_cmd.cpp src/cli/commands/recall_cmd.cpp
git commit -m "feat: phase-02 ICM memory store + BM25 scorer"
```

---

## Amendments from Security & Architecture Review

### CRITICAL Fixes

**A1 — Scorer re-fit on POST_STORE**
Tambahkan hook di `src/imem/hooks/scorer_hook.cpp`:
```cpp
ICMG_REGISTER_HOOK(HookEvent::POST_STORE, [](HookContext& ctx) {
    // Invalidate scorer — will re-fit on next recall
    Scorer::instance().invalidate();
}, Priority::LOW);
```
Scorer::recall() lazy-refit: jika `dirty_ = true`, panggil fit() sebelum rank.

### HIGH Fixes

**A2 — Memory deduplication at store time**
Tambahkan ke Task 1 (MemoryStore::store):
```cpp
// Jaccard similarity check sebelum insert
auto similar = findSimilar(node.topic, node.content, threshold=0.85);
if (!similar.empty()) {
    // return {id: existing.id, duplicate: true} atau throw dengan message
    throw DuplicateError("Similar to node #" + similar[0].id + ". Use --force to store anyway.");
}
```
Flag: `icmg store ... --force` untuk bypass.

**A3 — TTL/expiry support**
Tambahkan kolom ke schema (migration baru):
```sql
ALTER TABLE memory_nodes ADD COLUMN expires_at INTEGER DEFAULT NULL;
```
- `icmg store ... --ttl 30d` → set expires_at = now + 30 days
- Cleanup: daemon Phase 04 purge expired nodes setiap hari
- `icmg recall` filter `WHERE expires_at IS NULL OR expires_at > now()`

**A4 — Soft delete / undo**
Tambahkan:
```sql
ALTER TABLE memory_nodes ADD COLUMN deleted_at INTEGER DEFAULT NULL;
```
- `icmg forget <id>` → set deleted_at = now (bukan DELETE)
- `icmg restore <id>` → set deleted_at = NULL
- `icmg memory purge` → hard DELETE semua yang deleted_at < 30 hari lalu
- recall query: `WHERE deleted_at IS NULL`

**A5 — Confirmation prompt untuk destructive ops**
```cpp
// icmg forget <id> tanpa --yes
if (!args.hasFlag("yes")) {
    auto node = store.get(id);
    std::cerr << "Delete: [" << node.topic << "] \"" << node.content.substr(0,80) << "\"?\n";
    std::cerr << "Add --yes to confirm.\n";
    return 1;
}
```
Berlaku juga untuk: `icmg sp remove`, `icmg rule remove`, `icmg data remove`.

### MEDIUM Additions

**A6 — Fuzzy search fallback**
```cpp
// Jika BM25 return 0 results DAN --fuzzy flag:
if (results.empty() && args.fuzzy) {
    results = store.fuzzySearch(query, limit);  // edit-distance fallback
}
```
Implementasi: BK-tree atau simple Levenshtein per token.

**A7 — Score explainability**
`icmg recall "query" --explain` output:
```
[170.3] preferences — "caveman mode aktif"
  BM25=67.2 × recency=0.94 × freq=1.79 × importance=1.5 = 170.3
  Matched tokens: [caveman(tf=1), mode(tf=2)]
```

**A8 — Query history tracking**
Tambahkan tabel:
```sql
CREATE TABLE recall_history (
    id INTEGER PRIMARY KEY,
    query TEXT NOT NULL,
    result_count INTEGER,
    created_at INTEGER
);
```
`icmg recall --history` → tampilkan 20 query terakhir.

**A9 — Keyword indexing**
Ganti keywords TEXT (comma-separated) dengan junction table:
```sql
CREATE TABLE memory_keywords (
    node_id INTEGER REFERENCES memory_nodes(id) ON DELETE CASCADE,
    keyword TEXT NOT NULL,
    PRIMARY KEY (node_id, keyword)
);
CREATE INDEX idx_memory_keywords_kw ON memory_keywords(keyword);
```
