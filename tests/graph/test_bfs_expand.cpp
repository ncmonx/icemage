// tests/graph/test_bfs_expand.cpp
// BFS expansion feature tests: shortestPath, closureByLevel, commonAncestors, impactAll.
#include "../test_main.hpp"

int main() { return icmg::test::run_all(); }
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include <string>
#include <vector>
#include <algorithm>

using namespace icmg;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static core::Db makeBfsDb() {
    core::Db db(":memory:");
    db.run("PRAGMA foreign_keys=ON");
    db.run(
        "CREATE TABLE graph_nodes("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " path TEXT NOT NULL UNIQUE,"
        " lang TEXT DEFAULT '',"
        " context TEXT DEFAULT '',"
        " symbols TEXT DEFAULT '',"
        " size_bytes INTEGER DEFAULT 0,"
        " file_hash TEXT DEFAULT '',"
        " access_count INTEGER NOT NULL DEFAULT 0,"
        " updated_at INTEGER NOT NULL DEFAULT 0,"
        " group_id TEXT,"
        " zone TEXT NOT NULL DEFAULT 'default',"
        " parent_id INTEGER,"
        " kind TEXT NOT NULL DEFAULT 'file',"
        " symbol_name TEXT,"
        " signature TEXT,"
        " line_start INTEGER,"
        " line_end INTEGER,"
        " body_hash TEXT"
        ")"
    );
    db.run(
        "CREATE TABLE graph_edges("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " src INTEGER NOT NULL,"
        " dst INTEGER NOT NULL,"
        " edge_type TEXT NOT NULL DEFAULT 'imports',"
        " weight REAL DEFAULT 1.0,"
        " UNIQUE(src,dst,edge_type)"
        ")"
    );
    return db;
}

static void insertNode(core::Db& db, const std::string& path) {
    db.run("INSERT OR IGNORE INTO graph_nodes(path) VALUES(?)", {path});
}

static void insertEdge(core::Db& db, const std::string& src, const std::string& dst,
                       const std::string& type = "imports") {
    db.run(
        "INSERT OR IGNORE INTO graph_edges(src,dst,edge_type)"
        " SELECT s.id,d.id,? FROM graph_nodes s,graph_nodes d"
        " WHERE s.path=? AND d.path=?",
        {type, src, dst}
    );
}

// ---------------------------------------------------------------------------
// shortestPath tests
// ---------------------------------------------------------------------------

TEST("shortestPath: direct edge") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts");
    insertEdge(db, "a.ts", "b.ts");
    graph::GraphStore store(db);
    auto path = store.shortestPath("a.ts", "b.ts");
    ASSERT_TRUE(path.size() == 2);
    ASSERT_TRUE(path[0] == "a.ts");
    ASSERT_TRUE(path[1] == "b.ts");
}

TEST("shortestPath: two hops") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts"); insertNode(db, "c.ts");
    insertEdge(db, "a.ts", "b.ts"); insertEdge(db, "b.ts", "c.ts");
    graph::GraphStore store(db);
    auto path = store.shortestPath("a.ts", "c.ts");
    ASSERT_TRUE(path.size() == 3);
    ASSERT_TRUE(path[0] == "a.ts");
    ASSERT_TRUE(path[1] == "b.ts");
    ASSERT_TRUE(path[2] == "c.ts");
}

TEST("shortestPath: no path returns empty") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts");
    graph::GraphStore store(db);
    auto path = store.shortestPath("a.ts", "b.ts");
    ASSERT_TRUE(path.empty());
}

TEST("shortestPath: same node returns single") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts");
    graph::GraphStore store(db);
    auto path = store.shortestPath("a.ts", "a.ts");
    ASSERT_TRUE(path.size() == 1);
    ASSERT_TRUE(path[0] == "a.ts");
}

TEST("shortestPath: unknown node returns empty") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts");
    graph::GraphStore store(db);
    auto path = store.shortestPath("a.ts", "nonexistent.ts");
    ASSERT_TRUE(path.empty());
}

// ---------------------------------------------------------------------------
// closureByLevel tests
// ---------------------------------------------------------------------------

TEST("closureByLevel: two levels") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts"); insertNode(db, "c.ts");
    insertEdge(db, "a.ts", "b.ts"); insertEdge(db, "b.ts", "c.ts");
    graph::GraphStore store(db);
    auto n = store.getNode("a.ts");
    ASSERT_TRUE(n.has_value());
    auto levels = store.closureByLevel(n->id, {}, 5, false);
    ASSERT_TRUE(levels.size() == 2);
    ASSERT_TRUE(levels[0].size() == 1 && levels[0][0].path == "b.ts");
    ASSERT_TRUE(levels[1].size() == 1 && levels[1][0].path == "c.ts");
}

TEST("closureByLevel: empty when no edges") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts");
    graph::GraphStore store(db);
    auto n = store.getNode("a.ts");
    ASSERT_TRUE(n.has_value());
    auto levels = store.closureByLevel(n->id, {}, 5, false);
    ASSERT_TRUE(levels.empty());
}

TEST("closureByLevel: reverse (inbound)") {
    auto db = makeBfsDb();
    insertNode(db, "lib.ts"); insertNode(db, "app.ts");
    insertEdge(db, "app.ts", "lib.ts");
    graph::GraphStore store(db);
    auto n = store.getNode("lib.ts");
    ASSERT_TRUE(n.has_value());
    auto levels = store.closureByLevel(n->id, {}, 5, /*reverse=*/true);
    ASSERT_TRUE(levels.size() == 1);
    ASSERT_TRUE(levels[0][0].path == "app.ts");
}

// ---------------------------------------------------------------------------
// commonAncestors tests
// ---------------------------------------------------------------------------

TEST("commonAncestors: shared root") {
    auto db = makeBfsDb();
    insertNode(db, "root.ts");
    insertNode(db, "a.ts"); insertNode(db, "b.ts");
    insertEdge(db, "root.ts", "a.ts"); insertEdge(db, "root.ts", "b.ts");
    graph::GraphStore store(db);
    auto common = store.commonAncestors("a.ts", "b.ts");
    ASSERT_TRUE(common.size() == 1);
    ASSERT_TRUE(common[0].path == "root.ts");
}

TEST("commonAncestors: no common returns empty") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts"); insertNode(db, "b.ts");
    graph::GraphStore store(db);
    auto common = store.commonAncestors("a.ts", "b.ts");
    ASSERT_TRUE(common.empty());
}

TEST("commonAncestors: unknown node returns empty") {
    auto db = makeBfsDb();
    insertNode(db, "a.ts");
    graph::GraphStore store(db);
    auto common = store.commonAncestors("a.ts", "nonexistent.ts");
    ASSERT_TRUE(common.empty());
}

// ---------------------------------------------------------------------------
// impact() edge-type filter test
// ---------------------------------------------------------------------------

TEST("impact with edge-type filter") {
    auto db = makeBfsDb();
    insertNode(db, "lib.ts"); insertNode(db, "app.ts"); insertNode(db, "test.ts");
    insertEdge(db, "app.ts",  "lib.ts", "imports");
    insertEdge(db, "test.ts", "lib.ts", "calls");
    graph::GraphStore store(db);
    auto all          = store.impact("lib.ts", 3, {});
    auto imports_only = store.impact("lib.ts", 3, {"imports"});
    ASSERT_TRUE(all.size() == 2);
    ASSERT_TRUE(imports_only.size() == 1 && imports_only[0].path == "app.ts");
}

// ---------------------------------------------------------------------------
// impactAll tests
// ---------------------------------------------------------------------------

TEST("impactAll: two sources union") {
    auto db = makeBfsDb();
    insertNode(db, "lib1.ts"); insertNode(db, "lib2.ts");
    insertNode(db, "app1.ts"); insertNode(db, "app2.ts");
    insertEdge(db, "app1.ts", "lib1.ts"); insertEdge(db, "app2.ts", "lib2.ts");
    graph::GraphStore store(db);
    auto all = store.impactAll({"lib1.ts", "lib2.ts"}, 3);
    ASSERT_TRUE(all.size() == 2);
}

TEST("impactAll: empty sources returns empty") {
    auto db = makeBfsDb();
    graph::GraphStore store(db);
    auto all = store.impactAll({}, 3);
    ASSERT_TRUE(all.empty());
}
