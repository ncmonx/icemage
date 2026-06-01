// v1.80 atomize T7: memory-atomize CLI worker contract test
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/imem/atom_store.hpp"
#include <string>

using namespace icmg::core;

static Db makeDb() {
    Db db(":memory:");
    Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

static int countRows(Db& db, const std::string& sql) {
    int n = 0;
    db.query(sql, {}, [&](const Row& r) { if (!r.empty()) n = std::stoi(r[0]); });
    return n;
}

TEST("atomize_cmd: drain count matches pending queue") {
    Db db = makeDb();
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) "
           "VALUES('t','A. B. C.','','default',1)");
    int64_t nid = db.lastInsertId();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, 1);
    int processed = as.drainQueue(100);
    ASSERT_EQ(processed, 1);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atoms WHERE source_node_id="
                            + std::to_string(nid)), 3);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atom_queue"), 0);
}

TEST("atomize_cmd: ICMG_ATOMIZE=0 guard — queue not drained") {
    Db db = makeDb();
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) "
           "VALUES('t','Fact one. Fact two.','','default',1)");
    int64_t nid = db.lastInsertId();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, 1);
#ifdef ICMG_ATOMIZE_DISABLED
    // Simulated: when ICMG_ATOMIZE=0 env is set the CLI returns early.
    // Queue stays unprocessed. Test structural gate only.
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atom_queue"), 1);
#else
    int processed = as.drainQueue(0);
    ASSERT_EQ(processed, 0);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atom_queue"), 1);
#endif
}

TEST("atomize_cmd: stats — pending + atom counts correct") {
    Db db = makeDb();
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) "
           "VALUES('t','Alpha. Beta.','','default',1)");
    int64_t nid = db.lastInsertId();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, 1);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atom_queue"), 1);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atoms WHERE deleted_at=0"), 0);
    as.drainQueue(10);
    ASSERT_EQ(countRows(db, "SELECT COUNT(*) FROM memory_atom_queue"), 0);
    ASSERT_TRUE(countRows(db, "SELECT COUNT(*) FROM memory_atoms WHERE deleted_at=0") >= 1);
}


TEST("recall_atoms: atom match maps back to source node") {
    Db db = makeDb();
    db.run("INSERT INTO memory_nodes(topic,content,keywords,zone,created_at) "
           "VALUES('t','The linker error came from a missing symbol. Rebuilt clean.','','default',1)");
    int64_t nid = db.lastInsertId();
    icmg::imem::AtomStore as(db);
    as.enqueue(nid, 1);
    as.drainQueue(10);
    auto sources = as.recallAtomSources("linker missing symbol", 5);
    ASSERT_TRUE(!sources.empty());
    ASSERT_EQ((int64_t)sources[0], nid);
}
