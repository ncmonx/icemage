// v1.79.0 ICM dual-memory — atom schema + AtomStore tests.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
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
