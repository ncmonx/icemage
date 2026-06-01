// v1.79.0 ICM dual-memory — atom schema + AtomStore tests.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/imem/atom_store.hpp"
#include <string>

using namespace icmg::core;

static Db makeDb() {
    Db db(":memory:");
    Migrator migrator("__nonexistent_migrations_dir__");  // force embedded path
    migrator.runAll(db);
    return db;
}

static int countRows(Db& db, const std::string& sql) {
    int n = 0;
    db.query(sql, {}, [&](const Row& r) { if (!r.empty()) n = std::stoi(r[0]); });
    return n;
}

TEST("atom schema: memory_atoms + queue tables exist after migrate") {
    Db db = makeDb();
    db.run("INSERT INTO memory_atoms(source_node_id,content,keywords,zone,created_at) "
           "VALUES(1,'fact one','k','default',100)");
    db.run("INSERT INTO memory_atom_queue(node_id,enqueued_at) VALUES(1,100)");
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atoms"), 1);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atom_queue"), 1);
}

TEST("atom_store: enqueue then drain inserts atoms + clears queue") {
    Db db = makeDb();
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) "
           "VALUES('t','Fix bug. Added test. Shipped fix.','','default','1000')");
    int64_t nid = db.lastInsertId();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, 1000);
    int processed = as.drainQueue(10);
    ASSERT_EQ(processed, 1);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atoms WHERE source_node_id=" + std::to_string(nid)), 3);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atom_queue"), 0);
}

TEST("atom_store: dedup within source collapses identical proposition") {
    Db db = makeDb();
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) "
           "VALUES('t','Same fact. Same fact.','','default','1')");
    int64_t nid = db.lastInsertId();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, 1);
    as.drainQueue(10);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atoms WHERE source_node_id=" + std::to_string(nid)), 1);
}

TEST("atom_store: recallAtomSources maps atom match to source node") {
    Db db = makeDb();
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) "
           "VALUES('t','The linker error came from a missing symbol. Rebuilt clean.','','default','1')");
    int64_t nid = db.lastInsertId();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, 1);
    as.drainQueue(10);
    auto sources = as.recallAtomSources("linker missing symbol", 5);
    ASSERT_TRUE(!sources.empty());
    ASSERT_EQ((int)sources[0], (int)nid);
}

#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"

TEST("store: enqueues node for atomization, does not block (no atoms yet)") {
    Db db = makeDb();
    icmg::imem::MemoryStore ms(db);
    icmg::imem::MemoryNode n;
    n.topic = "t"; n.content = "Fact one. Fact two."; n.zone = "default";
    int64_t id = ms.store(n, true);
    ASSERT_TRUE(id > 0);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atom_queue WHERE node_id=" + std::to_string(id)), 1);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atoms"), 0);
}
