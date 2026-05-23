// Test migration 0031: token_counts cache table and index creation.
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

// Test 1: token_counts table exists after migration.
TEST("migration_0031: token_counts table exists") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(tableExists(db, "token_counts"));
}

// Test 2: idx_token_counts_mtime index exists.
TEST("migration_0031: idx_token_counts_mtime index exists") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(indexExists(db, "idx_token_counts_mtime"));
}

// Test 3: PRIMARY KEY constraint on path (duplicate INSERT without REPLACE -> error).
TEST("migration_0031: PRIMARY KEY rejects duplicate path") {
    auto db = makeFullyMigratedDb();
    db.run("INSERT INTO token_counts (path, tokens, bytes, mtime) VALUES (?, ?, ?, ?)",
           {"src/foo.cpp", "42", "1024", "1700000000"});
    bool threw = false;
    try {
        db.run("INSERT INTO token_counts (path, tokens, bytes, mtime) VALUES (?, ?, ?, ?)",
               {"src/foo.cpp", "99", "2048", "1700000001"});
    } catch (...) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
