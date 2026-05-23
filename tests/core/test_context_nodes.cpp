// Unit tests for ContextNodeStore — upsert/get/list/search/tier/BM25/diff.
#include "../test_main.hpp"
#include "../../src/core/context_node_store.hpp"
#include "../../src/core/db.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace icmg::core;

// ---- helper: in-memory DB --------------------------------------------------

static Db makeTestDb() {
    return Db(":memory:");
}

static void applyMigration(Db& db) {
    db.run(
        "CREATE TABLE IF NOT EXISTS context_nodes ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  node_key    TEXT    NOT NULL UNIQUE,"
        "  title       TEXT    NOT NULL,"
        "  content     TEXT    NOT NULL,"
        "  source_file TEXT    NOT NULL DEFAULT '',"
        "  tier        TEXT    NOT NULL DEFAULT 'cold',"
        "  tags        TEXT    NOT NULL DEFAULT '[]',"
        "  active      INTEGER NOT NULL DEFAULT 1,"
        "  created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        "  updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );
}

static ContextNode makeNode(const std::string& key, const std::string& title,
                             const std::string& content, const std::string& tier = "cold") {
    ContextNode n;
    n.node_key    = key;
    n.title       = title;
    n.content     = content;
    n.tier        = tier;
    n.source_file = "test.md";
    n.tags        = "[]";
    n.active      = true;
    return n;
}

// ---- tests -----------------------------------------------------------------

TEST("context_nodes: upsert + get roundtrip") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    auto node = makeNode("project-overview", "Project Overview",
                         "icmg is a single C++ binary.", "hot");
    store.upsert(node);

    auto got = store.get("project-overview");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->title,   std::string("Project Overview"));
    ASSERT_EQ(got->tier,    std::string("hot"));
    ASSERT_EQ(got->content, std::string("icmg is a single C++ binary."));
}

TEST("context_nodes: upsert is idempotent (UPSERT ON CONFLICT)") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    auto n1 = makeNode("arch", "Architecture", "Component A", "hot");
    store.upsert(n1);

    auto n2 = makeNode("arch", "Architecture", "Component A updated", "cold");
    store.upsert(n2);

    auto got = store.get("arch");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->content, std::string("Component A updated"));
    ASSERT_EQ(got->tier,    std::string("cold"));
    ASSERT_EQ(store.count(), 1);
}

TEST("context_nodes: list all + tier filter") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeNode("k1", "T1", "C1", "hot"));
    store.upsert(makeNode("k2", "T2", "C2", "cold"));
    store.upsert(makeNode("k3", "T3", "C3", "skill"));

    auto all  = store.list("",     true);
    auto hot  = store.list("hot",  true);
    auto cold = store.list("cold", true);

    ASSERT_EQ((int)all.size(),  3);
    ASSERT_EQ((int)hot.size(),  1);
    ASSERT_EQ((int)cold.size(), 1);
    ASSERT_EQ(hot[0].node_key, std::string("k1"));
}

TEST("context_nodes: setActive + list active_only") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeNode("a", "A", "content a", "cold"));
    store.upsert(makeNode("b", "B", "content b", "cold"));

    store.setActive("a", false);

    auto active_only = store.list("cold", true);
    auto all         = store.list("cold", false);

    ASSERT_EQ((int)active_only.size(), 1);
    ASSERT_EQ(active_only[0].node_key, std::string("b"));
    ASSERT_EQ((int)all.size(), 2);
}

TEST("context_nodes: remove") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeNode("x", "X", "to delete", "cold"));
    ASSERT_EQ(store.count(), 1);

    store.remove("x");
    ASSERT_EQ(store.count(), 0);
    ASSERT_FALSE(store.get("x").has_value());
}

TEST("context_nodes: BM25 search returns relevant nodes") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeNode("cmake", "Build Instructions",
        "Use cmake -B build to configure. Then cmake --build build.", "hot"));
    store.upsert(makeNode("overview", "Project Overview",
        "icmg is a single binary for memory and graph.", "hot"));
    store.upsert(makeNode("mcp", "MCP Server",
        "14 MCP tools available via stdio transport.", "cold"));

    auto results = store.search("cmake build configure", "", 3, 0.0);
    ASSERT_TRUE(!results.empty());
    ASSERT_EQ(results[0].node_key, std::string("cmake"));
}

TEST("context_nodes: BM25 tier filter respected in search") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeNode("build-hot",  "Build Hot",  "cmake build configure", "hot"));
    store.upsert(makeNode("build-cold", "Build Cold", "cmake build configure", "cold"));

    auto hot_only = store.search("cmake build", "hot", 5, 0.0);
    ASSERT_EQ((int)hot_only.size(), 1);
    ASSERT_EQ(hot_only[0].node_key, std::string("build-hot"));
}

TEST("context_nodes: empty query returns all active nodes up to limit") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    for (int i = 0; i < 5; ++i)
        store.upsert(makeNode("k" + std::to_string(i), "T" + std::to_string(i),
                              "content", "hot"));

    auto all = store.search("", "hot", 3, 0.0);
    ASSERT_EQ((int)all.size(), 3);
}

TEST("context_nodes: count by tier") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeNode("h1", "H1", "c", "hot"));
    store.upsert(makeNode("h2", "H2", "c", "hot"));
    store.upsert(makeNode("c1", "C1", "c", "cold"));

    ASSERT_EQ(store.count("hot"),  2);
    ASSERT_EQ(store.count("cold"), 1);
    ASSERT_EQ(store.count(),       3);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
