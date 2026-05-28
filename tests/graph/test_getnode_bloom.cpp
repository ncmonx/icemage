// v1.59 F3: getNode bloom-gate + tightened path-component suffix match.
//
// Verifies:
//   - exact path lookup still works (bloom maybe-present -> SQL hit)
//   - basename lookup still works (stored basename == query basename)
//   - absent path returns nullopt (bloom gate or SQL miss)
//   - tightened suffix: 'xbar.cpp' no longer matches stored '.../bar.cpp'
//     (old over-broad `LIKE %bar.cpp` regression)

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include "../../src/graph/graph_node.hpp"

using namespace icmg;

namespace {

core::Db makeDb() {
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
    return db;
}

graph::GraphNode mkFile(const std::string& path) {
    graph::GraphNode n;
    n.path = path; n.lang = "cpp"; n.kind = "file";
    return n;
}

}  // namespace

TEST("getNode bloom: exact path still resolves") {
    auto db = makeDb();
    graph::GraphStore g(db);
    g.upsertNode(mkFile("src/foo/bar.cpp"));
    auto n = g.getNode("src/foo/bar.cpp");
    ASSERT_TRUE(n.has_value());
    ASSERT_EQ(n->path, std::string("src/foo/bar.cpp"));
}

TEST("getNode bloom: basename lookup still resolves") {
    auto db = makeDb();
    graph::GraphStore g(db);
    g.upsertNode(mkFile("src/foo/bar.cpp"));
    auto n = g.getNode("bar.cpp");
    ASSERT_TRUE(n.has_value());
    ASSERT_EQ(n->path, std::string("src/foo/bar.cpp"));
}

TEST("getNode bloom: absent path returns nullopt") {
    auto db = makeDb();
    graph::GraphStore g(db);
    g.upsertNode(mkFile("src/foo/bar.cpp"));
    auto n = g.getNode("totally/absent/nothere.zzz");
    ASSERT_FALSE(n.has_value());
}

TEST("getNode: tightened suffix — 'xbar.cpp' does NOT match 'bar.cpp'") {
    auto db = makeDb();
    graph::GraphStore g(db);
    g.upsertNode(mkFile("src/foo/bar.cpp"));
    // Old over-broad `LIKE %bar.cpp` wrongly matched this; now must miss.
    auto n = g.getNode("xbar.cpp");
    ASSERT_FALSE(n.has_value());
}

TEST("getNode bloom: node added after seed is found (incremental sync)") {
    auto db = makeDb();
    graph::GraphStore g(db);
    g.upsertNode(mkFile("a/one.cpp"));
    // Force seed via a lookup.
    (void)g.getNode("a/one.cpp");
    // Now add a new node; bloom must be updated incrementally.
    g.upsertNode(mkFile("b/two.cpp"));
    auto n = g.getNode("b/two.cpp");
    ASSERT_TRUE(n.has_value());
    ASSERT_EQ(n->path, std::string("b/two.cpp"));
}

TEST("getNode bloom: opt-out env still resolves correctly") {
    // With bloom disabled, behaviour must be identical (just no gate).
#ifdef _WIN32
    _putenv_s("ICMG_NO_GRAPH_BLOOM", "1");
#else
    setenv("ICMG_NO_GRAPH_BLOOM", "1", 1);
#endif
    auto db = makeDb();
    graph::GraphStore g(db);
    g.upsertNode(mkFile("src/x.cpp"));
    ASSERT_TRUE(g.getNode("src/x.cpp").has_value());
    ASSERT_FALSE(g.getNode("nope.cpp").has_value());
#ifdef _WIN32
    _putenv_s("ICMG_NO_GRAPH_BLOOM", "");
#else
    unsetenv("ICMG_NO_GRAPH_BLOOM");
#endif
}
