// tests/cli/test_memory_export_cmd.cpp
// TDD for icmg memory export (v2.8.2).
// Tests MemoryStore::all() -- the bulk-export API used by memory-export command.

#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include <string>

using namespace icmg;

static core::Db makeDb() {
    core::Db db(":memory:");
    core::Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

static imem::MemoryNode makeNode(const std::string& content, const std::string& topic,
                                  const std::string& zone = "default") {
    imem::MemoryNode n;
    n.content = content;
    n.topic   = topic;
    n.zone    = zone;
    return n;
}

// ---------------------------------------------------------------------------
// MemoryStore::all() bulk export
// ---------------------------------------------------------------------------
TEST("memory_export: empty store produces empty list") {
    auto db = makeDb();
    imem::MemoryStore store(db);
    auto nodes = store.all();
    ASSERT_EQ(nodes.size(), (size_t)0);
}

TEST("memory_export: stored node retrievable via all()") {
    auto db = makeDb();
    imem::MemoryStore store(db);
    store.store(makeNode("export content", "export-topic"));
    auto nodes = store.all();
    ASSERT_EQ(nodes.size(), (size_t)1);
    ASSERT_EQ(nodes[0].topic, std::string("export-topic"));
    ASSERT_TRUE(nodes[0].content.find("export content") != std::string::npos);
}

TEST("memory_export: multiple nodes all returned") {
    auto db = makeDb();
    imem::MemoryStore store(db);
    store.store(makeNode("alpha", "topic-a"));
    store.store(makeNode("beta",  "topic-b"));
    store.store(makeNode("gamma", "topic-c"));
    auto nodes = store.all();
    ASSERT_EQ(nodes.size(), (size_t)3);
}

TEST("memory_export: node has required export fields (id, topic, content, created_at)") {
    auto db = makeDb();
    imem::MemoryStore store(db);
    store.store(makeNode("field check content", "field-topic"));
    auto nodes = store.all();
    ASSERT_EQ(nodes.size(), (size_t)1);
    auto& n = nodes[0];
    ASSERT_TRUE(n.id > 0);
    ASSERT_TRUE(!n.topic.empty());
    ASSERT_TRUE(!n.content.empty());
    ASSERT_TRUE(n.created_at > 0);
}

TEST("memory_export: zone field preserved in all()") {
    auto db = makeDb();
    imem::MemoryStore store(db);
    store.store(makeNode("zone content", "zone-topic", "my-zone"));
    auto nodes = store.all();
    ASSERT_EQ(nodes.size(), (size_t)1);
    ASSERT_EQ(nodes[0].zone, std::string("my-zone"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
