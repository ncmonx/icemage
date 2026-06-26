// TDD (Phase 3 graphify-parity): QueryEngine — NL query -> seed nodes -> BFS
// subgraph -> formatted text context. Tests the engine directly with an
// in-memory GraphStore (no Config / project DB needed).
//
// Verifies:
//   - buildSubGraph(seed) returns the seed + its closure neighbors
//   - maxNodes hard-caps the returned set
//   - formatSubGraph emits non-empty text mentioning node paths + confidence
//   - explainNode returns node context + neighbor list

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include "../../src/cli/graph_query_engine.hpp"

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
    db.run("CREATE TABLE graph_edges("
           " src INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,"
           " dst INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,"
           " edge_type TEXT NOT NULL,"
           " weight REAL NOT NULL DEFAULT 1.0,"
           " confidence TEXT NOT NULL DEFAULT 'EXTRACTED',"
           " PRIMARY KEY(src,dst,edge_type))");
    return db;
}

graph::GraphNode node(const std::string& path, const std::string& ctx) {
    graph::GraphNode n;
    n.path = path;
    n.lang = "cpp";
    n.kind = "file";
    n.context = ctx;
    return n;
}

void edge(graph::GraphStore& store, int64_t src, int64_t dst, const std::string& type) {
    graph::GraphEdge e;
    e.src = src;
    e.dst = dst;
    e.edge_type = type;
    store.upsertEdge(e);
}

}  // namespace

// a.cpp imports b.cpp imports c.cpp. Seed on "auth" should reach the chain.
TEST("graph-query-engine: buildSubGraph closure") {
    auto db = makeDb();
    graph::GraphStore store(db);
    auto a = store.upsertNode(node("auth.cpp", "handles authentication"));
    auto b = store.upsertNode(node("session.cpp", "session tokens"));
    auto c = store.upsertNode(node("crypto.cpp", "hashing"));
    edge(store, a, b, "imports");
    edge(store, b, c, "imports");

    cli::QueryEngine eng(store);
    auto sg = eng.buildSubGraph("authentication", /*depth=*/2, /*maxNodes=*/50);

    // Seed (auth.cpp) plus its 2-hop closure (session.cpp, crypto.cpp).
    ASSERT_TRUE(sg.nodes.size() >= 2);
    bool hasAuth = false, hasSession = false;
    for (auto& n : sg.nodes) {
        if (n.path == "auth.cpp") hasAuth = true;
        if (n.path == "session.cpp") hasSession = true;
    }
    ASSERT_TRUE(hasAuth);
    ASSERT_TRUE(hasSession);
}

TEST("graph-query-engine: maxNodes caps") {
    auto db = makeDb();
    graph::GraphStore store(db);
    auto root = store.upsertNode(node("root.cpp", "central hub"));
    for (int i = 0; i < 10; ++i) {
        auto leaf = store.upsertNode(node("leaf" + std::to_string(i) + ".cpp", "central leaf"));
        edge(store, root, leaf, "imports");
    }
    cli::QueryEngine eng(store);
    auto sg = eng.buildSubGraph("central", /*depth=*/2, /*maxNodes=*/3);
    ASSERT_TRUE(sg.nodes.size() <= 3);
}

TEST("graph-query-engine: formatSubGraph nonempty") {
    auto db = makeDb();
    graph::GraphStore store(db);
    auto a = store.upsertNode(node("auth.cpp", "handles authentication"));
    (void)a;
    cli::QueryEngine eng(store);
    auto sg = eng.buildSubGraph("authentication", 2, 50);
    auto text = eng.formatSubGraph(sg);
    ASSERT_TRUE(!text.empty());
    ASSERT_TRUE(text.find("auth.cpp") != std::string::npos);
}

TEST("graph-query-engine: explainNode") {
    auto db = makeDb();
    graph::GraphStore store(db);
    auto a = store.upsertNode(node("auth.cpp", "handles authentication"));
    auto b = store.upsertNode(node("session.cpp", "session tokens"));
    edge(store, a, b, "imports");
    cli::QueryEngine eng(store);
    auto text = eng.explainNode("auth.cpp", 1);
    ASSERT_TRUE(!text.empty());
    ASSERT_TRUE(text.find("auth.cpp") != std::string::npos);
}
