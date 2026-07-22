// tests/mcp/test_graph_query_tool.cpp
// v2.20 research #7: deterministic multi-hop graph MCP tool
// (blast_radius | who_calls | path_between).
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/graph_store.hpp"
#include "../../src/mcp/base_mcp_tool.hpp"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;
using namespace icmg;

static core::Db makeGraphDb() {
    core::Db db(":memory:");
    db.run("PRAGMA foreign_keys=ON");
    db.run("CREATE TABLE graph_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL UNIQUE,"
           " lang TEXT, context TEXT, symbols TEXT, size_bytes INTEGER,"
           " file_hash TEXT, access_count INTEGER NOT NULL DEFAULT 0,"
           " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           " group_id TEXT, zone TEXT NOT NULL DEFAULT 'default',"
           " parent_id INTEGER REFERENCES graph_nodes(id) ON DELETE CASCADE,"
           " kind TEXT NOT NULL DEFAULT 'file', symbol_name TEXT, signature TEXT,"
           " line_start INTEGER, line_end INTEGER, body_hash TEXT)");
    db.run("CREATE TABLE graph_edges("
           " src INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,"
           " dst INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,"
           " edge_type TEXT NOT NULL, weight REAL NOT NULL DEFAULT 1.0,"
           " confidence TEXT NOT NULL DEFAULT 'EXTRACTED',"
           " PRIMARY KEY(src,dst,edge_type))");
    return db;
}

// Build A -> B -> C (edge src imports dst). So C is imported by B, B by A.
// impact(x) = reverse closure = who (transitively) depends on x.
static void seedChain(core::Db& db) {
    graph::GraphNode a; a.path = "a.cpp"; a.lang = "cpp";
    graph::GraphNode b; b.path = "b.cpp"; b.lang = "cpp";
    graph::GraphNode c; c.path = "c.cpp"; c.lang = "cpp";
    graph::GraphStore store(db);
    int64_t ida = store.upsertNode(a);
    int64_t idb = store.upsertNode(b);
    int64_t idc = store.upsertNode(c);
    auto edge = [&](int64_t s, int64_t d) {
        db.run("INSERT INTO graph_edges(src,dst,edge_type) VALUES(" +
               std::to_string(s) + "," + std::to_string(d) + ",'imports')");
    };
    edge(ida, idb);   // a imports b
    edge(idb, idc);   // b imports c
}

static std::unique_ptr<BaseMcpTool> makeTool() {
    return core::Registry<BaseMcpTool>::instance().create("icmg_graph_query");
}

TEST("graph_query: tool is registered") {
    auto t = makeTool();
    ASSERT_TRUE(t != nullptr);
    ASSERT_EQ(t->name(), std::string("icmg_graph_query"));
}

TEST("graph_query: who_calls returns direct dependents only") {
    auto db = makeGraphDb();
    seedChain(db);
    auto t = makeTool();
    json r = t->call({{"op","who_calls"},{"path","c.cpp"}}, db);
    // b imports c directly -> b is a 1-hop dependent of c
    ASSERT_TRUE(r.contains("dependents"));
    bool hasB = false, hasA = false;
    for (auto& d : r["dependents"]) {
        if (d["path"] == "b.cpp") hasB = true;
        if (d["path"] == "a.cpp") hasA = true;
    }
    ASSERT_TRUE(hasB);
    ASSERT_TRUE(!hasA);   // a is 2 hops away, excluded at depth 1
}

TEST("graph_query: blast_radius reaches transitive dependents") {
    auto db = makeGraphDb();
    seedChain(db);
    auto t = makeTool();
    json r = t->call({{"op","blast_radius"},{"path","c.cpp"},{"depth",5}}, db);
    bool hasA = false, hasB = false;
    for (auto& d : r["dependents"]) {
        if (d["path"] == "a.cpp") hasA = true;
        if (d["path"] == "b.cpp") hasB = true;
    }
    ASSERT_TRUE(hasA && hasB);   // both transitively depend on c
}

TEST("graph_query: path_between finds a dependency path") {
    auto db = makeGraphDb();
    seedChain(db);
    auto t = makeTool();
    json r = t->call({{"op","path_between"},{"path","a.cpp"},{"to","c.cpp"}}, db);
    ASSERT_TRUE(r.contains("reachable"));
    ASSERT_EQ(r["reachable"].get<bool>(), true);
    ASSERT_TRUE(r["path"].size() >= 2u);
}

TEST("graph_query: path_between requires 'to'") {
    auto db = makeGraphDb();
    seedChain(db);
    auto t = makeTool();
    json r = t->call({{"op","path_between"},{"path","a.cpp"}}, db);
    ASSERT_TRUE(r.contains("error"));
}

TEST("graph_query: unknown op errors cleanly") {
    auto db = makeGraphDb();
    seedChain(db);
    auto t = makeTool();
    json r = t->call({{"op","frobnicate"},{"path","a.cpp"}}, db);
    ASSERT_TRUE(r.contains("error"));
}

TEST("graph_query: annotations read-only + not open-world") {
    auto t = makeTool();
    auto aj = t->annotationsJson();
    ASSERT_EQ(aj["readOnlyHint"].get<bool>(), true);
    ASSERT_EQ(aj["openWorldHint"].get<bool>(), false);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
