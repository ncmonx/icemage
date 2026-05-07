// Phase 24: dedupeCaseMixedPaths regression test.
// Reproduces the v0.6.1 bug where Windows drive-case differences (d:\... vs D:\...)
// produced duplicate graph_nodes that, after edge resolution, looked like cycles.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"

using namespace icmg;

static core::Db makeDb() {
    core::Db db(":memory:");
    db.run("PRAGMA foreign_keys=ON");
    db.run(
        "CREATE TABLE graph_nodes("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " path TEXT NOT NULL UNIQUE,"
        " lang TEXT, context TEXT, symbols TEXT,"
        " size_bytes INTEGER, file_hash TEXT,"
        " access_count INTEGER NOT NULL DEFAULT 0,"
        " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " group_id TEXT, zone TEXT NOT NULL DEFAULT 'default',"
        " parent_id INTEGER REFERENCES graph_nodes(id) ON DELETE CASCADE,"
        " kind TEXT NOT NULL DEFAULT 'file',"
        " symbol_name TEXT, signature TEXT,"
        " line_start INTEGER, line_end INTEGER, body_hash TEXT)");
    db.run("CREATE TABLE graph_edges("
           " src INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,"
           " dst INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,"
           " edge_type TEXT NOT NULL,"
           " weight REAL NOT NULL DEFAULT 1.0,"
           " PRIMARY KEY(src,dst,edge_type))");
    return db;
}

static int countNodes(core::Db& db) {
    int n = 0;
    db.query("SELECT COUNT(*) FROM graph_nodes", {},
             [&](const core::Row& r) { if (!r.empty()) try { n = std::stoi(r[0]); } catch (...) {} });
    return n;
}

TEST("dedupe: case-mixed paths collapse to one row") {
    auto db = makeDb();
    db.run("INSERT INTO graph_nodes(path, lang) VALUES(?,?)",
           {"d:\\proj\\Foo.cs", "csharp"});
    db.run("INSERT INTO graph_nodes(path, lang) VALUES(?,?)",
           {"D:\\proj\\Foo.cs", "csharp"});
    ASSERT_EQ(countNodes(db), 2);

    graph::GraphStore store(db);
    int merged = store.dedupeCaseMixedPaths();
    ASSERT_EQ(merged, 1);
    ASSERT_EQ(countNodes(db), 1);
}

TEST("dedupe: edges reparent to survivor (no FK violation)") {
    auto db = makeDb();
    db.run("INSERT INTO graph_nodes(path) VALUES(?)", {"d:\\proj\\A.cs"});
    db.run("INSERT INTO graph_nodes(path) VALUES(?)", {"D:\\proj\\A.cs"});
    db.run("INSERT INTO graph_nodes(path) VALUES(?)", {"d:\\proj\\B.cs"});
    int64_t a_lower=0, a_upper=0, b=0;
    db.query("SELECT id FROM graph_nodes WHERE path=?", {"d:\\proj\\A.cs"},
             [&](const core::Row& r){ a_lower = std::stoll(r[0]); });
    db.query("SELECT id FROM graph_nodes WHERE path=?", {"D:\\proj\\A.cs"},
             [&](const core::Row& r){ a_upper = std::stoll(r[0]); });
    db.query("SELECT id FROM graph_nodes WHERE path=?", {"d:\\proj\\B.cs"},
             [&](const core::Row& r){ b = std::stoll(r[0]); });
    // Edge from A_lower -> B (this row gets deleted; edge must reparent to keeper).
    db.run("INSERT INTO graph_edges(src,dst,edge_type) VALUES(?,?,?)",
           {std::to_string(a_lower), std::to_string(b), "imports"});

    graph::GraphStore store(db);
    int merged = store.dedupeCaseMixedPaths();
    ASSERT_EQ(merged, 1);

    // Edge total survives (1 row). Whichever id became keeper, dst=B unchanged.
    int n = 0;
    db.query("SELECT COUNT(*) FROM graph_edges WHERE dst=?",
             {std::to_string(b)}, [&](const core::Row& r){
                 if (!r.empty()) n = std::stoi(r[0]);
             });
    ASSERT_EQ(n, 1);
    (void)a_upper;
}

TEST("dedupe: idempotent — second call merges 0") {
    auto db = makeDb();
    db.run("INSERT INTO graph_nodes(path) VALUES(?)", {"d:\\X.cs"});
    db.run("INSERT INTO graph_nodes(path) VALUES(?)", {"D:\\X.cs"});
    graph::GraphStore store(db);
    ASSERT_EQ(store.dedupeCaseMixedPaths(), 1);
    ASSERT_EQ(store.dedupeCaseMixedPaths(), 0);
}

TEST("dedupe: self-loop pruned after reparent") {
    auto db = makeDb();
    db.run("INSERT INTO graph_nodes(path) VALUES(?)", {"d:\\Self.cs"});
    db.run("INSERT INTO graph_nodes(path) VALUES(?)", {"D:\\Self.cs"});
    int64_t lo = 0, up = 0;
    db.query("SELECT id FROM graph_nodes WHERE path=?", {"d:\\Self.cs"},
             [&](const core::Row& r){ lo = std::stoll(r[0]); });
    db.query("SELECT id FROM graph_nodes WHERE path=?", {"D:\\Self.cs"},
             [&](const core::Row& r){ up = std::stoll(r[0]); });
    // Edge upper -> lower would become lo -> lo (self-loop) post-merge.
    db.run("INSERT INTO graph_edges(src,dst,edge_type) VALUES(?,?,?)",
           {std::to_string(up), std::to_string(lo), "imports"});

    graph::GraphStore store(db);
    store.dedupeCaseMixedPaths();
    int loops = 0;
    db.query("SELECT COUNT(*) FROM graph_edges WHERE src=dst", {},
             [&](const core::Row& r){ if (!r.empty()) loops = std::stoi(r[0]); });
    ASSERT_EQ(loops, 0);
}

int main() {
    std::cout << "=== graph::dedupeCaseMixedPaths tests ===\n";
    return icmg::test::run_all();
}
