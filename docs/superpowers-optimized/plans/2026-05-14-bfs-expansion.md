# BFS Graph Expansion Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:subagent-driven-development (recommended) or superpowers-optimized:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 7 BFS graph commands (shortest-path, layers, neighbors, common-ancestors, edge-type filter, DOT export, multi-source impact) + auto-route them into Claude's session awareness from day 1.

**Architecture:** New methods in `GraphStore` (graph_store.cpp/.hpp). New commands appended in `graph_cmd.cpp`. One new test file `tests/graph/test_bfs_expand.cpp`. AGENTS_BLOCK in `init_cmd.cpp` updated so `icmg init` / `icmg upgrade` pushes routing table to every project's AGENTS.md automatically.

**Tech Stack:** C++17, SQLite WAL, icmg `BaseCommand` + `ICMG_REGISTER_COMMAND`, in-memory SQLite for tests.

**Assumptions:**
- Assumes `graph_edges` schema: `src INTEGER, dst INTEGER, edge_type TEXT` — will NOT work if schema differs.
- Assumes `core::Db::query(sql, bind, callback)` interface unchanged — will NOT compile if signature changes.
- Assumes graph DB populated via `icmg graph scan` — empty DB returns empty results (not errors).
- Edge type values: `imports`, `calls`, `inherits`, `includes` — `--edge-type` filter accepts any string but only these match real data.

---

## Files

**Modify:**
- `src/graph/graph_store.hpp` — add 4 new method declarations
- `src/graph/graph_store.cpp` — implement 4 new methods
- `src/cli/commands/graph_cmd.cpp` — add 7 new command classes + REGISTER macros + extend existing commands
- `src/cli/commands/init_cmd.cpp` — extend AGENTS_BLOCK routing table with 7 new rows

**Create:**
- `tests/graph/test_bfs_expand.cpp` — unit tests for all 4 new GraphStore methods

**Modify (build):**
- `CMakeLists.txt` — add test_bfs_expand executable

---

## Task 1: GraphStore — `shortestPath()` method + test

**Files:**
- Modify: `src/graph/graph_store.hpp`
- Modify: `src/graph/graph_store.cpp`
- Create: `tests/graph/test_bfs_expand.cpp`
- Modify: `CMakeLists.txt`

**Does NOT cover:** reverse (dst→src) path. Only follows forward edges (src→dst). For reverse, caller should swap arguments.

- [ ] **Step 1: Write failing test**

```cpp
// tests/graph/test_bfs_expand.cpp
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include <string>
#include <vector>

using namespace icmg;

static core::Db makeBfsDb() {
    core::Db db(":memory:");
    db.run("PRAGMA foreign_keys=ON");
    db.run("CREATE TABLE graph_nodes(id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL UNIQUE, lang TEXT DEFAULT '', context TEXT DEFAULT '', symbols TEXT DEFAULT '', size_bytes INTEGER DEFAULT 0, file_hash TEXT DEFAULT '', access_count INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL DEFAULT 0, group_id TEXT, zone TEXT NOT NULL DEFAULT 'default', parent_id INTEGER, kind TEXT NOT NULL DEFAULT 'file', symbol_name TEXT, signature TEXT, line_start INTEGER, line_end INTEGER, body_hash TEXT)");
    db.run("CREATE TABLE graph_edges(id INTEGER PRIMARY KEY AUTOINCREMENT, src INTEGER NOT NULL, dst INTEGER NOT NULL, edge_type TEXT NOT NULL DEFAULT 'imports', weight REAL DEFAULT 1.0, UNIQUE(src,dst,edge_type))");
    return db;
}

static void insertNode(core::Db& db, const std::string& path) {
    db.run("INSERT OR IGNORE INTO graph_nodes(path) VALUES(?)", {path});
}
static void insertEdge(core::Db& db, const std::string& src, const std::string& dst) {
    db.run("INSERT OR IGNORE INTO graph_edges(src,dst,edge_type) SELECT s.id,d.id,'imports' FROM graph_nodes s,graph_nodes d WHERE s.path=? AND d.path=?", {src, dst});
}

TEST("shortestPath: direct edge") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts");
    insertEdge(db, "a.ts", "b.ts");
    graph::GraphStore store(db);
    auto path = store.shortestPath("a.ts", "b.ts");
    ASSERT(path.size() == 2);
    ASSERT(path[0] == "a.ts");
    ASSERT(path[1] == "b.ts");
}

TEST("shortestPath: two hops") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts"); insertNode(db, "c.ts");
    insertEdge(db, "a.ts", "b.ts"); insertEdge(db, "b.ts", "c.ts");
    graph::GraphStore store(db);
    auto path = store.shortestPath("a.ts", "c.ts");
    ASSERT(path.size() == 3);
    ASSERT(path[0] == "a.ts"); ASSERT(path[1] == "b.ts"); ASSERT(path[2] == "c.ts");
}

TEST("shortestPath: no path returns empty") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts");
    graph::GraphStore store(db);
    auto path = store.shortestPath("a.ts", "b.ts");
    ASSERT(path.empty());
}

TEST("shortestPath: same node returns single") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts");
    graph::GraphStore store(db);
    auto path = store.shortestPath("a.ts", "a.ts");
    ASSERT(path.size() == 1);
    ASSERT(path[0] == "a.ts");
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

Find the block with `add_executable(test_graph_edges ...)` in `CMakeLists.txt` and append after it:
```cmake
add_executable(test_bfs_expand tests/graph/test_bfs_expand.cpp)
target_link_libraries(test_bfs_expand PRIVATE icmg_lib)
add_test(NAME test_bfs_expand COMMAND test_bfs_expand)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: FAIL — `store.shortestPath` does not exist yet.

- [ ] **Step 4: Add declaration to graph_store.hpp**

After the `closure()` declaration (line ~38), add:
```cpp
    // BFS shortest path (forward edges src→dst). Returns ordered path from `from` to `to`,
    // empty if unreachable. edge_types empty → all types.
    std::vector<std::string> shortestPath(const std::string& from,
                                           const std::string& to,
                                           const std::vector<std::string>& edge_types = {},
                                           int max_depth = 30);
```

- [ ] **Step 5: Implement in graph_store.cpp**

Add after `closure()` implementation (after line ~336):
```cpp
std::vector<std::string> GraphStore::shortestPath(
    const std::string& from, const std::string& to,
    const std::vector<std::string>& edge_types, int max_depth)
{
    auto src = getNode(from);
    auto dst = getNode(to);
    if (!src || !dst) return {};
    if (src->id == dst->id) return {from};

    std::string type_clause;
    std::vector<std::string> params;
    if (!edge_types.empty()) {
        type_clause = " AND edge_type IN (";
        for (size_t i = 0; i < edge_types.size(); ++i) {
            if (i) type_clause += ",";
            type_clause += "?";
            params.push_back(edge_types[i]);
        }
        type_clause += ")";
    }

    std::unordered_map<int64_t, int64_t> parent;
    std::deque<std::pair<int64_t,int>> q;
    q.push_back({src->id, 0});
    parent[src->id] = -1;
    bool found = false;

    while (!q.empty() && !found) {
        auto [cur, d] = q.front(); q.pop_front();
        if (d >= max_depth) continue;
        std::string sql = "SELECT dst FROM graph_edges WHERE src=?" + type_clause;
        std::vector<std::string> bind = {std::to_string(cur)};
        bind.insert(bind.end(), params.begin(), params.end());
        db_.query(sql, bind, [&](const core::Row& r) {
            if (r.empty() || found) return;
            int64_t nb;
            try { nb = std::stoll(r[0]); } catch (...) { return; }
            if (parent.count(nb)) return;
            parent[nb] = cur;
            if (nb == dst->id) { found = true; return; }
            q.push_back({nb, d + 1});
        });
    }
    if (!found) return {};

    std::vector<int64_t> ids;
    int64_t cur = dst->id;
    while (cur != -1) {
        ids.push_back(cur);
        cur = parent.at(cur);
    }
    std::reverse(ids.begin(), ids.end());

    std::vector<std::string> result;
    for (int64_t id : ids) {
        db_.query("SELECT path FROM graph_nodes WHERE id=?", {std::to_string(id)},
                  [&](const core::Row& r) { if (!r.empty()) result.push_back(r[0]); });
    }
    return result;
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: PASS — all 4 shortestPath tests green.

- [ ] **Step 7: Commit**

```bash
git add src/graph/graph_store.hpp src/graph/graph_store.cpp tests/graph/test_bfs_expand.cpp CMakeLists.txt
git commit -m "feat: GraphStore::shortestPath() — BFS with parent-pointer reconstruction"
```

---

## Task 2: CLI — `graph path <A> <B>` command

**Files:**
- Modify: `src/cli/commands/graph_cmd.cpp`

**Does NOT cover:** reverse path. Only forward edges.

- [ ] **Step 1: Add command class in graph_cmd.cpp**

Before the `ICMG_REGISTER_COMMAND` block at the bottom of `graph_cmd.cpp`, add:
```cpp
// ---- graph path ----
class GraphPathCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-path"; }
    std::string description() const override { return "BFS shortest path between two files"; }
    int run(const std::vector<std::string>& args) override {
        std::vector<std::string> positional;
        for (auto& a : args) { if (!a.empty() && a[0] != '-') positional.push_back(a); }
        if (positional.size() < 2) {
            std::cerr << "Usage: icmg graph path <from> <to> [--depth N] [--edge-type TYPE]\n";
            return 1;
        }
        std::string from = positional[0], to = positional[1];
        int max_depth = 30;
        try { max_depth = std::stoi(flagValue(args, "--depth", "30")); } catch (...) {}
        std::vector<std::string> edge_types;
        std::string et = flagValue(args, "--edge-type", "");
        if (!et.empty()) edge_types.push_back(et);

        core::Config& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath());
        graph::GraphStore store(db);
        auto path = store.shortestPath(from, to, edge_types, max_depth);

        if (path.empty()) {
            std::cout << "No path found from " << from << " to " << to << "\n";
            return 1;
        }
        std::cout << "Path (" << path.size() - 1 << " hop(s)):\n";
        for (size_t i = 0; i < path.size(); ++i) {
            if (i) std::cout << "  → ";
            else   std::cout << "  ";
            std::cout << path[i] << "\n";
        }
        return 0;
    }
};
```

- [ ] **Step 2: Register command**

In the ICMG_REGISTER_COMMAND block at the bottom of `graph_cmd.cpp`, add:
```cpp
ICMG_REGISTER_COMMAND("graph-path", GraphPathCommand);
```

- [ ] **Step 3: Wire `graph path` alias in GraphRootCommand**

In `GraphRootCommand::run()`, the existing `compound = "graph-" + args[0]` dispatch already handles this — no change needed. Verify by checking that `args[0] == "path"` → routes to `graph-path`.

- [ ] **Step 4: Build and smoke-test**

Run: `cmake --build build --parallel`
Expected: builds without errors.
Smoke: `./build/icmg.exe graph path nonexistent.ts other.ts` → prints "No path found".

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/graph_cmd.cpp
git commit -m "feat: graph path <A> <B> — BFS shortest path CLI command"
```

---

## Task 3: GraphStore — `closureByLevel()` + test

**Files:**
- Modify: `src/graph/graph_store.hpp`
- Modify: `src/graph/graph_store.cpp`
- Modify: `tests/graph/test_bfs_expand.cpp`

**Does NOT cover:** nodes at depth-0 (start node itself not in any returned level). Level index 0 = direct neighbors.

- [ ] **Step 1: Add tests to test_bfs_expand.cpp**

Append to `tests/graph/test_bfs_expand.cpp`:
```cpp
TEST("closureByLevel: two levels") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts"); insertNode(db, "c.ts");
    insertEdge(db, "a.ts", "b.ts"); insertEdge(db, "b.ts", "c.ts");
    graph::GraphStore store(db);
    auto n = store.getNode("a.ts");
    auto levels = store.closureByLevel(n->id, {}, 5, false);
    ASSERT(levels.size() == 2);
    ASSERT(levels[0].size() == 1 && levels[0][0].path == "b.ts");
    ASSERT(levels[1].size() == 1 && levels[1][0].path == "c.ts");
}

TEST("closureByLevel: empty when no edges") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts");
    graph::GraphStore store(db);
    auto n = store.getNode("a.ts");
    auto levels = store.closureByLevel(n->id, {}, 5, false);
    ASSERT(levels.empty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: FAIL — `closureByLevel` does not exist.

- [ ] **Step 3: Add declaration to graph_store.hpp**

After `shortestPath()` declaration:
```cpp
    // BFS returning nodes grouped by distance level. Level 0 = direct neighbors.
    // reverse=true walks inbound edges.
    std::vector<std::vector<GraphNode>> closureByLevel(int64_t start,
                                                        const std::vector<std::string>& edge_types,
                                                        int max_depth,
                                                        bool reverse = false);
```

- [ ] **Step 4: Implement in graph_store.cpp**

Add after `shortestPath()` implementation:
```cpp
std::vector<std::vector<GraphNode>> GraphStore::closureByLevel(
    int64_t start, const std::vector<std::string>& edge_types,
    int max_depth, bool reverse)
{
    std::string type_clause;
    std::vector<std::string> params;
    if (!edge_types.empty()) {
        type_clause = " AND edge_type IN (";
        for (size_t i = 0; i < edge_types.size(); ++i) {
            if (i) type_clause += ",";
            type_clause += "?";
            params.push_back(edge_types[i]);
        }
        type_clause += ")";
    }
    std::string col_pick  = reverse ? "src" : "dst";
    std::string col_where = reverse ? "dst" : "src";

    std::unordered_set<int64_t> visited;
    visited.insert(start);
    std::vector<int64_t> frontier = {start};
    std::vector<std::vector<GraphNode>> levels;

    for (int d = 0; d < max_depth && !frontier.empty(); ++d) {
        std::vector<int64_t> next;
        std::vector<GraphNode> level_nodes;
        for (int64_t cur : frontier) {
            std::string sql = "SELECT " + col_pick + " FROM graph_edges WHERE "
                            + col_where + "=?" + type_clause;
            std::vector<std::string> bind = {std::to_string(cur)};
            bind.insert(bind.end(), params.begin(), params.end());
            db_.query(sql, bind, [&](const core::Row& r) {
                if (r.empty()) return;
                int64_t nb; try { nb = std::stoll(r[0]); } catch (...) { return; }
                if (!visited.insert(nb).second) return;
                next.push_back(nb);
                db_.query("SELECT id,path,lang,context,symbols,size_bytes,file_hash,"
                          "updated_at,access_count,zone,parent_id,kind,symbol_name,"
                          "signature,line_start,line_end,body_hash FROM graph_nodes WHERE id=?",
                          {std::to_string(nb)},
                          [&](const core::Row& nr) { level_nodes.push_back(rowToNode(nr)); });
            });
        }
        if (!level_nodes.empty()) levels.push_back(std::move(level_nodes));
        frontier = std::move(next);
    }
    return levels;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/graph/graph_store.hpp src/graph/graph_store.cpp tests/graph/test_bfs_expand.cpp
git commit -m "feat: GraphStore::closureByLevel() — BFS grouped by distance level"
```

---

## Task 4: CLI — `graph layers <file>` command

**Files:**
- Modify: `src/cli/commands/graph_cmd.cpp`

- [ ] **Step 1: Add command class**

Before the ICMG_REGISTER_COMMAND block:
```cpp
// ---- graph layers ----
class GraphLayersCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-layers"; }
    std::string description() const override { return "Show dependency layers (BFS depth groups)"; }
    int run(const std::vector<std::string>& args) override {
        std::string file;
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { file = a; break; } }
        if (file.empty()) { std::cerr << "Usage: icmg graph layers <file> [--depth N] [--reverse] [--edge-type TYPE]\n"; return 1; }
        int max_depth = 10;
        try { max_depth = std::stoi(flagValue(args, "--depth", "10")); } catch (...) {}
        bool rev = hasFlag(args, "--reverse");
        std::vector<std::string> edge_types;
        std::string et = flagValue(args, "--edge-type", "");
        if (!et.empty()) edge_types.push_back(et);

        core::Config& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath());
        graph::GraphStore store(db);
        auto node = store.getNode(file);
        if (!node) { std::cerr << "Node not found: " << file << "\n"; return 1; }

        auto levels = store.closureByLevel(node->id, edge_types, max_depth, rev);
        if (levels.empty()) { std::cout << "No " << (rev ? "dependents" : "dependencies") << " found.\n"; return 0; }

        std::cout << "depth-0: " << file << "\n";
        for (size_t i = 0; i < levels.size(); ++i) {
            std::cout << "depth-" << (i + 1) << ": (" << levels[i].size() << " file(s))\n";
            for (auto& n : levels[i]) std::cout << "  " << n.path << "\n";
        }
        return 0;
    }
};
```

- [ ] **Step 2: Register**

```cpp
ICMG_REGISTER_COMMAND("graph-layers", GraphLayersCommand);
```

- [ ] **Step 3: Build and smoke-test**

Run: `cmake --build build --parallel`
Expected: builds without errors.

- [ ] **Step 4: Commit**

```bash
git add src/cli/commands/graph_cmd.cpp
git commit -m "feat: graph layers <file> — dependency layers grouped by BFS depth"
```

---

## Task 5: CLI — `graph neighbors <file> --radius N`

**Files:**
- Modify: `src/cli/commands/graph_cmd.cpp`

Uses existing `closure()` — no new GraphStore method needed.

- [ ] **Step 1: Add command class**

```cpp
// ---- graph neighbors ----
class GraphNeighborsCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-neighbors"; }
    std::string description() const override { return "All nodes within N hops (bidirectional BFS)"; }
    int run(const std::vector<std::string>& args) override {
        std::string file;
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { file = a; break; } }
        if (file.empty()) { std::cerr << "Usage: icmg graph neighbors <file> [--radius N] [--edge-type TYPE]\n"; return 1; }
        int radius = 2;
        try { radius = std::stoi(flagValue(args, "--radius", "2")); } catch (...) {}
        std::vector<std::string> edge_types;
        std::string et = flagValue(args, "--edge-type", "");
        if (!et.empty()) edge_types.push_back(et);

        core::Config& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath());
        graph::GraphStore store(db);
        auto node = store.getNode(file);
        if (!node) { std::cerr << "Node not found: " << file << "\n"; return 1; }

        // Forward + reverse BFS, union of both
        auto fwd = store.closure(node->id, edge_types, radius, false);
        auto rev = store.closure(node->id, edge_types, radius, true);
        std::unordered_set<int64_t> seen(fwd.begin(), fwd.end());
        for (int64_t id : rev) seen.insert(id);

        std::cout << file << " — " << seen.size() << " neighbor(s) within " << radius << " hop(s):\n";
        for (int64_t id : seen) {
            db.query("SELECT path FROM graph_nodes WHERE id=?", {std::to_string(id)},
                     [](const core::Row& r) { if (!r.empty()) std::cout << "  " << r[0] << "\n"; });
        }
        return 0;
    }
};
```

- [ ] **Step 2: Register**

```cpp
ICMG_REGISTER_COMMAND("graph-neighbors", GraphNeighborsCommand);
```

- [ ] **Step 3: Build**

Run: `cmake --build build --parallel`
Expected: builds without errors.

- [ ] **Step 4: Commit**

```bash
git add src/cli/commands/graph_cmd.cpp
git commit -m "feat: graph neighbors <file> --radius N — bidirectional BFS hop radius"
```

---

## Task 6: GraphStore — `commonAncestors()` + test

**Files:**
- Modify: `src/graph/graph_store.hpp`
- Modify: `src/graph/graph_store.cpp`
- Modify: `tests/graph/test_bfs_expand.cpp`

- [ ] **Step 1: Add tests**

Append to `tests/graph/test_bfs_expand.cpp`:
```cpp
TEST("commonAncestors: shared root") {
    auto db = makeBfsDb();
    insertNode(db, "root.ts");
    insertNode(db, "a.ts"); insertNode(db, "b.ts");
    insertEdge(db, "root.ts", "a.ts"); insertEdge(db, "root.ts", "b.ts");
    graph::GraphStore store(db);
    auto common = store.commonAncestors("a.ts", "b.ts");
    ASSERT(common.size() == 1);
    ASSERT(common[0].path == "root.ts");
}

TEST("commonAncestors: no common returns empty") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts");
    graph::GraphStore store(db);
    auto common = store.commonAncestors("a.ts", "b.ts");
    ASSERT(common.empty());
}
```

- [ ] **Step 2: Run to verify fail**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: FAIL — `commonAncestors` not declared.

- [ ] **Step 3: Add declaration to graph_store.hpp**

```cpp
    // BFS reverse from both nodes; returns intersection (shared upstream dependencies).
    std::vector<GraphNode> commonAncestors(const std::string& a,
                                            const std::string& b,
                                            int max_depth = 15);
```

- [ ] **Step 4: Implement in graph_store.cpp**

```cpp
std::vector<GraphNode> GraphStore::commonAncestors(
    const std::string& a, const std::string& b, int max_depth)
{
    auto na = getNode(a);
    auto nb = getNode(b);
    if (!na || !nb) return {};

    auto a_ids = closure(na->id, {}, max_depth, /*reverse=*/true);
    std::unordered_set<int64_t> a_set(a_ids.begin(), a_ids.end());

    auto b_ids = closure(nb->id, {}, max_depth, /*reverse=*/true);

    std::vector<GraphNode> result;
    for (int64_t id : b_ids) {
        if (!a_set.count(id)) continue;
        db_.query("SELECT id,path,lang,context,symbols,size_bytes,file_hash,"
                  "updated_at,access_count,zone,parent_id,kind,symbol_name,"
                  "signature,line_start,line_end,body_hash FROM graph_nodes WHERE id=?",
                  {std::to_string(id)},
                  [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    }
    return result;
}
```

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/graph/graph_store.hpp src/graph/graph_store.cpp tests/graph/test_bfs_expand.cpp
git commit -m "feat: GraphStore::commonAncestors() — BFS reverse intersection"
```

---

## Task 7: CLI — `graph common <A> <B>` command

**Files:**
- Modify: `src/cli/commands/graph_cmd.cpp`

- [ ] **Step 1: Add command class**

```cpp
// ---- graph common ----
class GraphCommonCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-common"; }
    std::string description() const override { return "Find shared ancestors of two files"; }
    int run(const std::vector<std::string>& args) override {
        std::vector<std::string> positional;
        for (auto& a : args) { if (!a.empty() && a[0] != '-') positional.push_back(a); }
        if (positional.size() < 2) {
            std::cerr << "Usage: icmg graph common <A> <B> [--depth N]\n"; return 1;
        }
        int depth = 15;
        try { depth = std::stoi(flagValue(args, "--depth", "15")); } catch (...) {}

        core::Config& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath());
        graph::GraphStore store(db);
        auto common = store.commonAncestors(positional[0], positional[1], depth);

        if (common.empty()) {
            std::cout << "No common ancestors between " << positional[0] << " and " << positional[1] << "\n";
            return 0;
        }
        std::cout << common.size() << " common ancestor(s):\n";
        for (auto& n : common) std::cout << "  " << n.path << "\n";
        return 0;
    }
};
```

- [ ] **Step 2: Register**

```cpp
ICMG_REGISTER_COMMAND("graph-common", GraphCommonCommand);
```

- [ ] **Step 3: Build and commit**

```bash
cmake --build build --parallel
git add src/cli/commands/graph_cmd.cpp
git commit -m "feat: graph common <A> <B> — shared ancestor detection"
```

---

## Task 8: GraphStore `impact()` edge-type filter + `--edge-type` CLI flag

**Files:**
- Modify: `src/graph/graph_store.hpp`
- Modify: `src/graph/graph_store.cpp`
- Modify: `src/cli/commands/graph_cmd.cpp`
- Modify: `tests/graph/test_bfs_expand.cpp`

**Does NOT cover:** forward impact filtering. Only reverse BFS (`impact()`) gets the filter. `transitive-impact` uses `closure()` directly — extend separately if needed.

- [ ] **Step 1: Add test**

Append to `tests/graph/test_bfs_expand.cpp`:
```cpp
TEST("impact with edge-type filter") {
    auto db = makeBfsDb();
    insertNode(db, "lib.ts"); insertNode(db, "app.ts"); insertNode(db, "test.ts");
    // app imports lib; test has 'calls' edge to lib
    db.run("INSERT OR IGNORE INTO graph_edges(src,dst,edge_type) SELECT s.id,d.id,'imports' FROM graph_nodes s,graph_nodes d WHERE s.path='app.ts' AND d.path='lib.ts'");
    db.run("INSERT OR IGNORE INTO graph_edges(src,dst,edge_type) SELECT s.id,d.id,'calls' FROM graph_nodes s,graph_nodes d WHERE s.path='test.ts' AND d.path='lib.ts'");
    graph::GraphStore store(db);
    auto all    = store.impact("lib.ts", 3, {});
    auto imports_only = store.impact("lib.ts", 3, {"imports"});
    ASSERT(all.size() == 2);
    ASSERT(imports_only.size() == 1 && imports_only[0].path == "app.ts");
}
```

- [ ] **Step 2: Run to verify fail**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: FAIL — `impact()` with 3 args not declared.

- [ ] **Step 3: Update graph_store.hpp**

Change:
```cpp
    std::vector<GraphNode> impact(const std::string& path, int depth = 3);
```
To:
```cpp
    // edge_types empty → all types (backward compat).
    std::vector<GraphNode> impact(const std::string& path,
                                   int depth = 3,
                                   const std::vector<std::string>& edge_types = {});
```

- [ ] **Step 4: Update graph_store.cpp `impact()` implementation**

Find the `impact()` implementation in `graph_store.cpp` (around line 435) and replace its BFS loop to pass edge_types to closure:
```cpp
std::vector<GraphNode> GraphStore::impact(const std::string& path, int depth,
                                           const std::vector<std::string>& edge_types)
{
    auto node = getNode(path);
    if (!node) return {};
    auto ids = closure(node->id, edge_types, depth, /*reverse=*/true);
    std::vector<GraphNode> result;
    for (int64_t id : ids) {
        db_.query("SELECT id,path,lang,context,symbols,size_bytes,file_hash,"
                  "updated_at,access_count,zone,parent_id,kind,symbol_name,"
                  "signature,line_start,line_end,body_hash FROM graph_nodes WHERE id=?",
                  {std::to_string(id)},
                  [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    }
    return result;
}
```

- [ ] **Step 5: Add `--edge-type` flag to graph-impact and graph-reverse-impact CLI commands**

In `GraphImpactCommand::run()` and `GraphReverseImpactCommand::run()`, after the `depth` variable:
```cpp
        std::vector<std::string> edge_types;
        std::string et = flagValue(args, "--edge-type", "");
        if (!et.empty()) edge_types.push_back(et);
```
Then pass `edge_types` to `store.impact(file, depth, edge_types)`.

- [ ] **Step 6: Run test to verify pass**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/graph/graph_store.hpp src/graph/graph_store.cpp src/cli/commands/graph_cmd.cpp tests/graph/test_bfs_expand.cpp
git commit -m "feat: impact() edge-type filter + --edge-type flag on graph-impact/reverse-impact"
```

---

## Task 9: `--format dot` for `graph impact`

**Files:**
- Modify: `src/cli/commands/graph_cmd.cpp`

**Does NOT cover:** layers or path commands. Only `graph-impact`.

- [ ] **Step 1: Extend GraphImpactCommand with DOT output**

In `GraphImpactCommand::run()`, after computing `affected`, add:
```cpp
        bool dot_fmt = (flagValue(args, "--format", "") == "dot");
        if (dot_fmt) {
            // Build edge list via closureByLevel for depth coloring
            auto node = store.getNode(file);
            auto levels = node ? store.closureByLevel(node->id, edge_types, depth, true) : decltype(store.closureByLevel(0,{},0))();
            // depth → color map
            static const char* colors[] = {"\"#e74c3c\"","\"#e67e22\"","\"#f1c40f\"","\"#2ecc71\"","\"#3498db\"","\"#9b59b6\""};
            std::unordered_map<std::string, int> node_depth;
            for (size_t i = 0; i < levels.size(); ++i)
                for (auto& n : levels[i]) node_depth[n.path] = (int)i;

            std::cout << "digraph impact {\n  node [shape=box fontname=monospace];\n";
            std::cout << "  \"" << file << "\" [style=filled fillcolor=\"#c0392b\" fontcolor=white label=\"" << file << "\"];\n";
            for (auto& n : affected) {
                int d = node_depth.count(n.path) ? node_depth[n.path] : 0;
                std::string col = colors[std::min(d, 5)];
                std::cout << "  \"" << n.path << "\" [style=filled fillcolor=" << col << "];\n";
            }
            // Edges: for each affected node, add edge from it to file (reverse impact)
            for (auto& n : affected)
                std::cout << "  \"" << n.path << "\" -> \"" << file << "\";\n";
            std::cout << "}\n";
            return 0;
        }
```

- [ ] **Step 2: Build and smoke-test**

Run: `cmake --build build --parallel`
Smoke: `./build/icmg.exe graph impact somefile.ts --format dot` → outputs `digraph impact {` header.

- [ ] **Step 3: Commit**

```bash
git add src/cli/commands/graph_cmd.cpp
git commit -m "feat: graph impact --format dot — DOT output with depth-colored nodes"
```

---

## Task 10: GraphStore `impactAll()` + `graph impact <dir> --all`

**Files:**
- Modify: `src/graph/graph_store.hpp`
- Modify: `src/graph/graph_store.cpp`
- Modify: `src/cli/commands/graph_cmd.cpp`
- Modify: `tests/graph/test_bfs_expand.cpp`

- [ ] **Step 1: Add test**

Append to `tests/graph/test_bfs_expand.cpp`:
```cpp
TEST("impactAll: two sources union") {
    auto db = makeBfsDb();
    insertNode(db, "lib1.ts"); insertNode(db, "lib2.ts");
    insertNode(db, "app1.ts"); insertNode(db, "app2.ts");
    insertEdge(db, "app1.ts", "lib1.ts"); insertEdge(db, "app2.ts", "lib2.ts");
    graph::GraphStore store(db);
    auto all = store.impactAll({"lib1.ts", "lib2.ts"}, 3);
    ASSERT(all.size() == 2);
}
```

- [ ] **Step 2: Run to verify fail**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Add declaration**

In `graph_store.hpp`:
```cpp
    // Multi-source reverse BFS — union of impact from all paths.
    std::vector<GraphNode> impactAll(const std::vector<std::string>& paths, int depth = 3);
```

- [ ] **Step 4: Implement**

```cpp
std::vector<GraphNode> GraphStore::impactAll(
    const std::vector<std::string>& paths, int depth)
{
    std::unordered_set<int64_t> visited;
    std::deque<std::pair<int64_t,int>> q;
    for (auto& p : paths) {
        auto n = getNode(p);
        if (!n) continue;
        if (visited.insert(n->id).second) q.push_back({n->id, 0});
    }
    std::vector<GraphNode> result;
    while (!q.empty()) {
        auto [cur, d] = q.front(); q.pop_front();
        if (d >= depth) continue;
        db_.query("SELECT src FROM graph_edges WHERE dst=?", {std::to_string(cur)},
                  [&](const core::Row& r) {
                      if (r.empty()) return;
                      int64_t nb; try { nb = std::stoll(r[0]); } catch (...) { return; }
                      if (!visited.insert(nb).second) return;
                      db_.query("SELECT id,path,lang,context,symbols,size_bytes,file_hash,"
                                "updated_at,access_count,zone,parent_id,kind,symbol_name,"
                                "signature,line_start,line_end,body_hash FROM graph_nodes WHERE id=?",
                                {std::to_string(nb)},
                                [&](const core::Row& nr) { result.push_back(rowToNode(nr)); });
                      q.push_back({nb, d + 1});
                  });
    }
    return result;
}
```

- [ ] **Step 5: Extend `graph-impact` CLI with `--all` flag**

In `GraphImpactCommand::run()`, before the existing `store.impact()` call, add:
```cpp
        if (hasFlag(args, "--all")) {
            // Collect all files in directory
            std::vector<std::string> sources;
            if (fs::is_directory(file)) {
                for (auto& e : fs::recursive_directory_iterator(file)) {
                    if (e.is_regular_file()) {
                        std::string p = e.path().string();
                        for (char& c : p) if (c == '\\') c = '/';
                        sources.push_back(p);
                    }
                }
            } else {
                sources.push_back(file);
            }
            auto affected = store.impactAll(sources, depth);
            std::cout << affected.size() << " file(s) impacted by " << sources.size() << " source(s):\n";
            for (auto& n : affected) std::cout << "  " << n.path << "\n";
            return 0;
        }
```

- [ ] **Step 6: Run test to verify pass**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_bfs_expand --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/graph/graph_store.hpp src/graph/graph_store.cpp src/cli/commands/graph_cmd.cpp tests/graph/test_bfs_expand.cpp
git commit -m "feat: impactAll() + graph impact --all — multi-source BFS impact"
```

---

## Task 11: Day-1 awareness — AGENTS_BLOCK routing table + session auto-inject

**Files:**
- Modify: `src/cli/commands/init_cmd.cpp`

**Does NOT cover:** CLAUDE.md update (managed separately by `icmg claudemd import`). Only AGENTS.md routing table via AGENTS_BLOCK.

After `icmg upgrade` + `icmg init` (or `icmg init --force`), every project's AGENTS.md gets the new BFS rows automatically. Claude reads AGENTS.md at session start → BFS commands in context from day 1.

- [ ] **Step 1: Add BFS rows to AGENTS_BLOCK in init_cmd.cpp**

Find the decision table in `AGENTS_BLOCK` (around the `| Find a function |` rows). Add these 7 rows after the existing `| Trace impact |` row:
```
| Shortest path between 2 files | `icmg graph path <A> <B>` (BFS, hop count shown) |
| View deps by depth layer | `icmg graph layers <file> --depth N` (level-0=direct) |
| All files within N hops | `icmg graph neighbors <file> --radius N` (bidirectional) |
| Shared dependencies of 2 files | `icmg graph common <A> <B>` (BFS ancestor intersect) |
| Impact by specific edge type | `icmg graph impact <file> --edge-type imports\|calls\|inherits` |
| Impact as DOT graph | `icmg graph impact <file> --format dot \| dot -Tsvg > out.svg` |
| Impact from whole directory | `icmg graph impact <dir> --all --depth N` (multi-source BFS) |
```

- [ ] **Step 2: Build + run `icmg init --force` to verify AGENTS.md update**

```bash
cmake --build build --parallel
./build/icmg.exe init --force
grep "graph path" AGENTS.md
```
Expected: `| Shortest path between 2 files |` line appears in AGENTS.md.

- [ ] **Step 3: Commit**

```bash
git add src/cli/commands/init_cmd.cpp
git commit -m "feat: BFS commands in AGENTS_BLOCK routing table — day-1 session awareness"
```

---

## Task 12: `icmg init` auto-injects full command reference into AGENTS.md

**Files:**
- Modify: `src/cli/commands/init_cmd.cpp`

**Goal:** Every `icmg init` (including on upgrade) writes a comprehensive `<!-- icmg:commands:start/end -->` block into AGENTS.md listing ALL icmg commands with usage. Claude reads AGENTS.md at session start → all features visible from day 1 without any extra steps.

**Does NOT cover:** CLAUDE.md update (managed by `icmg claudemd import`). Only AGENTS.md.

- [ ] **Step 1: Add `COMMANDS_BLOCK` static string to init_cmd.cpp**

After the `AGENTS_BLOCK` constant (before `class InitCommand`), add:

```cpp
static const char* COMMANDS_BLOCK = R"MD(<!-- icmg:commands:start -->
## icmg — Full Command Reference (auto-updated by `icmg init`)

> This section is rewritten on every `icmg init` / `icmg upgrade`. Do not edit manually.

### Graph Analysis
| Command | Usage | Description |
|---|---|---|
| `graph scan` | `icmg graph scan [path]` | Index file/symbol dependency graph |
| `graph context` | `icmg graph context <file>` | Show file context + symbols |
| `graph impact` | `icmg graph impact <file> [--depth N] [--edge-type T] [--format dot] [--all]` | Reverse BFS: who depends on this file |
| `graph reverse-impact` | `icmg graph reverse-impact <file> [--depth N] [--edge-type T]` | Detailed reverse impact |
| `graph transitive-impact` | `icmg graph transitive-impact <file> [--depth N]` | Forward transitive dependencies |
| `graph path` | `icmg graph path <A> <B> [--depth N] [--edge-type T]` | BFS shortest path between two files |
| `graph layers` | `icmg graph layers <file> [--depth N] [--reverse] [--edge-type T]` | Dependencies grouped by BFS depth level |
| `graph neighbors` | `icmg graph neighbors <file> [--radius N] [--edge-type T]` | All files within N hops (bidirectional) |
| `graph common` | `icmg graph common <A> <B> [--depth N]` | Shared ancestors of two files |
| `graph callers` | `icmg graph callers <symbol>` | Who calls this symbol |
| `graph callees` | `icmg graph callees <symbol>` | What this symbol calls |
| `graph symbol` | `icmg graph symbol <Name>` | Find symbol definition (30 lines, not 800) |
| `graph related` | `icmg graph related <file>` | Related files by shared imports |
| `graph cycles` | `icmg graph cycles` | Detect dependency cycles |
| `graph orphans` | `icmg graph orphans` | Files with no inbound edges |
| `graph hot` | `icmg graph hot [--days N]` | Most accessed files recently |
| `graph search` | `icmg graph search <query>` | Full-text search across graph |
| `graph list` | `icmg graph list [--lang L]` | List all indexed files |
| `graph stats` | `icmg graph stats` | Node/edge counts |
| `graph watch` | `icmg graph watch` | Auto-update graph on file changes |
| `graph update` | `icmg graph update [file]` | Re-scan changed files |

### Memory & Recall
| Command | Usage | Description |
|---|---|---|
| `recall` | `icmg recall "<query>"` | BM25 recall from memory |
| `recall --semantic` | `icmg recall "<query>" --semantic` | Semantic (embedding) recall |
| `cross-recall` | `icmg cross-recall "<query>"` | Recall across all projects |
| `store` | `icmg store --topic T "content"` | Store decision/fact |
| `memory list` | `icmg memory list` | Browse stored memories |
| `memory forget` | `icmg memory forget <id>` | Soft-delete a memory |
| `pack` | `icmg pack "<task>"` | 4KB context bundle for task |
| `context` | `icmg context <file>` | Graph + symbols + memory overlay |
| `fail store` | `icmg fail store "<task>" "<approach>" "<reason>"` | Record failed approach |
| `fail recall` | `icmg fail recall "<task>"` | Recall failed approaches |

### Token Efficiency
| Command | Usage | Description |
|---|---|---|
| `run` | `icmg run <cmd>` | Run command with Tkil filter (60-90% smaller) |
| `compress` | `icmg compress < file` | Compress large output with glossary |
| `shrink` | `icmg shrink` | Shrink to token budget |
| `expand` | `icmg expand` | Expand compressed text |
| `parallel` | `icmg parallel --task "..." --task "..."` | Run independent tasks concurrently |
| `fetch` | `icmg fetch <url>` | Cached + token-reduced URL fetch |
| `diff-summary` | `icmg diff-summary --ref HEAD~N` | Summarized git diff |
| `context-budget` | `icmg context-budget` | Show session token usage |

### Session & Workflow
| Command | Usage | Description |
|---|---|---|
| `wake-up` | `icmg wake-up` | Session briefing: decisions + phases + fixes |
| `session claim` | `icmg session claim` | Mark session as active |
| `session save` | `icmg session save <tag>` | Snapshot session |
| `distill` | `icmg distill auto` | Auto-distill assistant message to memory |
| `wflog save` | `icmg wflog save --goal "..." --decisions "..."` | Log workflow decision |
| `verify` | `icmg verify --command "<cmd>"` | Record verified test/build result |
| `explain` | `icmg explain "<error>"` | Explain error from memory + graph |
| `agent` | `icmg agent "<task>"` | Delegate task to LLM via pack+prompt |

### Maintenance & Health
| Command | Usage | Description |
|---|---|---|
| `doctor` | `icmg doctor` | Diagnose icmg issues |
| `health` | `icmg health` | System health check (8 categories) |
| `maintain` | `icmg maintain` | DB hygiene (vacuum, prune) |
| `backup snapshot` | `icmg backup snapshot` | Manual DB snapshot |
| `backup auto-on` | `icmg backup auto-on --interval 1h` | Schedule hourly snapshots |
| `sentinel auto-on` | `icmg sentinel auto-on --every 15m` | Watchdog: disk + audit checks |
| `update` | `icmg update` | Self-upgrade to latest release |
| `whats-new` | `icmg whats-new` | Release notes |
| `strict` | `icmg strict [on/off/status]` | Toggle strict native-tool blocking |
| `caveman` | `icmg caveman [on/off/status]` | Toggle ultra-terse response mode |
| `init` | `icmg init [--force]` | Re-install hooks + refresh AGENTS.md |
<!-- icmg:commands:end -->
)MD";
```

- [ ] **Step 2: Extend `installAgents()` to also write COMMANDS_BLOCK**

In `installAgents()` in `init_cmd.cpp`, after the existing AGENTS_BLOCK write logic, add:

```cpp
        // Also update the full commands reference block.
        const std::string cmd_start = "<!-- icmg:commands:start -->";
        const std::string cmd_end   = "<!-- icmg:commands:end -->";

        // Re-read the file after writing AGENTS_BLOCK (it may have changed).
        std::string updated;
        { std::ifstream f2(ap); std::ostringstream s2; s2 << f2.rdbuf(); updated = s2.str(); }

        if (updated.find(cmd_start) != std::string::npos) {
            auto ca = updated.find(cmd_start);
            auto cb = updated.find(cmd_end);
            if (cb != std::string::npos && cb > ca) {
                std::string before2 = updated.substr(0, ca);
                std::string after2  = updated.substr(cb + cmd_end.size());
                std::ofstream f2(ap);
                f2 << before2 << COMMANDS_BLOCK << after2;
                std::cout << "  + AGENTS.md (icmg commands block updated)\n";
            }
        } else {
            std::ofstream f2(ap, std::ios::app);
            if (!updated.empty() && updated.back() != '\n') f2 << "\n";
            f2 << "\n" << COMMANDS_BLOCK;
            std::cout << "  + AGENTS.md (icmg commands block appended)\n";
        }
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build --parallel
./build/icmg.exe init --force
grep "graph path" AGENTS.md
grep "icmg:commands:start" AGENTS.md
```
Expected: both lines found. AGENTS.md contains the full command table.

- [ ] **Step 4: Run `icmg init` again (idempotent check)**

```bash
./build/icmg.exe init --force
wc -l AGENTS.md
```
Expected: same line count as after first run — no duplicate blocks.

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/init_cmd.cpp
git commit -m "feat: icmg init auto-injects full command reference into AGENTS.md — day-1 awareness"
```

---

## Self-Review

**Spec coverage:**
- `graph path` ✓ Task 1-2
- `graph layers` ✓ Task 3-4
- `graph neighbors` ✓ Task 5
- `graph common` ✓ Task 6-7
- `--edge-type` filter ✓ Task 8
- `--format dot` ✓ Task 9
- `graph impact --all` ✓ Task 10
- Day-1 routing awareness ✓ Task 11
- Full command reference auto-injected on every init ✓ Task 12

**Placeholder scan:** None found. All tasks have actual code.

**Type consistency:**
- `closureByLevel` returns `std::vector<std::vector<GraphNode>>` — used consistently in Tasks 3, 4, 9.
- `shortestPath` returns `std::vector<std::string>` (paths, not nodes) — consistent in Tasks 1-2.
- `commonAncestors` returns `std::vector<GraphNode>` — consistent in Tasks 6-7.
- `impactAll` returns `std::vector<GraphNode>` — consistent in Tasks 10.
- `impact()` 3rd param `const std::vector<std::string>& edge_types = {}` — default empty = backward compat.
- `COMMANDS_BLOCK` markers `<!-- icmg:commands:start/end -->` distinct from AGENTS_BLOCK markers `<!-- icmg:start/end -->` — no collision.
