// Test migration 0030: rules_bank table and index creation.
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

static bool columnExists(Db& db, const std::string& table, const std::string& col) {
    bool found = false;
    db.query("PRAGMA table_info(" + table + ")", {},
             [&](const Row& r) { if (r.size() > 1 && r[1] == col) found = true; });
    return found;
}

TEST("migration_0030: rules_bank table exists") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(tableExists(db, "rules_bank"));
}

TEST("migration_0030: rules_bank has column id") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "rules_bank", "id"));
}

TEST("migration_0030: rules_bank has column path") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "rules_bank", "path"));
}

TEST("migration_0030: rules_bank has column content") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "rules_bank", "content"));
}

TEST("migration_0030: rules_bank has column tag") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "rules_bank", "tag"));
}

TEST("migration_0030: rules_bank has column active") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "rules_bank", "active"));
}

TEST("migration_0030: rules_bank has column updated_at") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "rules_bank", "updated_at"));
}

TEST("migration_0030: idx_rules_bank_active index exists") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(indexExists(db, "idx_rules_bank_active"));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
