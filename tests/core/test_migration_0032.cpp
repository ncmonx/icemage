// Test migration 0032: approaches table and index creation.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include <string>
#include <vector>

using namespace icmg::core;

static Db makeFullyMigratedDb() {
    Db db(":memory:");
    Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

static bool tableExists(Db& db, const std::string& name) {
    bool found = false;
    db.query("SELECT name FROM sqlite_master WHERE type='table' AND name=?",
             {name}, [&](const Row& r) { if (!r.empty()) found = true; });
    return found;
}

static bool indexExists(Db& db, const std::string& name) {
    bool found = false;
    db.query("SELECT name FROM sqlite_master WHERE type='index' AND name=?",
             {name}, [&](const Row& r) { if (!r.empty()) found = true; });
    return found;
}

// Test 1: approaches table exists after migration.
TEST("migration_0032: approaches table exists") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(tableExists(db, "approaches"));
}

// Test 2: idx_approaches_task index exists.
TEST("migration_0032: idx_approaches_task index exists") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(indexExists(db, "idx_approaches_task"));
}

// Test 3: CHECK constraint rejects invalid outcome value.
TEST("migration_0032: CHECK constraint rejects invalid outcome") {
    auto db = makeFullyMigratedDb();
    bool threw = false;
    try {
        db.run("INSERT INTO approaches (task, approach, outcome) VALUES (?, ?, ?)",
               {"test-task", "some-approach", "bogus"});
    } catch (...) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
