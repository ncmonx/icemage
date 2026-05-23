// Test migration 0028: skill_chunks table creation and schema validation.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include <string>
#include <vector>

using namespace icmg::core;

// Helper: apply all embedded migrations to an in-memory DB using the embedded
// fallback path (no migrations/ dir present for in-memory path).
static Db makeFullyMigratedDb() {
    Db db(":memory:");
    Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    return db;
}

// Helper: check if a table exists in sqlite_master.
static bool tableExists(Db& db, const std::string& table_name) {
    bool found = false;
    db.query(
        "SELECT name FROM sqlite_master WHERE type='table' AND name=?",
        {table_name},
        [&](const Row& row) {
            if (!row.empty()) found = true;
        }
    );
    return found;
}

// Helper: check if an index exists in sqlite_master.
static bool indexExists(Db& db, const std::string& index_name) {
    bool found = false;
    db.query(
        "SELECT name FROM sqlite_master WHERE type='index' AND name=?",
        {index_name},
        [&](const Row& row) {
            if (!row.empty()) found = true;
        }
    );
    return found;
}

// Helper: check if a column exists in a table via PRAGMA table_info.
// PRAGMA table_info columns: cid, name, type, notnull, dflt_value, pk
static bool columnExists(Db& db, const std::string& table_name,
                         const std::string& col_name) {
    bool found = false;
    db.query(
        "PRAGMA table_info(" + table_name + ")",
        {},
        [&](const Row& row) {
            // row[1] = column name
            if (row.size() > 1 && row[1] == col_name) {
                found = true;
            }
        }
    );
    return found;
}

// ---- tests ------------------------------------------------------------------

TEST("migration_0028: skill_chunks table exists after runAll") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(tableExists(db, "skill_chunks"));
}

TEST("migration_0028: skill_chunks has column id") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "skill_chunks", "id"));
}

TEST("migration_0028: skill_chunks has column skill_id") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "skill_chunks", "skill_id"));
}

TEST("migration_0028: skill_chunks has column parent_path") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "skill_chunks", "parent_path"));
}

TEST("migration_0028: skill_chunks has column heading") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "skill_chunks", "heading"));
}

TEST("migration_0028: skill_chunks has column content") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "skill_chunks", "content"));
}

TEST("migration_0028: skill_chunks has column token_count") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "skill_chunks", "token_count"));
}

TEST("migration_0028: skill_chunks has column embedding") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "skill_chunks", "embedding"));
}

TEST("migration_0028: skill_chunks has column created_at") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(columnExists(db, "skill_chunks", "created_at"));
}

TEST("migration_0028: idx_skill_chunks_skill_id index exists") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(indexExists(db, "idx_skill_chunks_skill_id"));
}

TEST("migration_0028: idx_skill_chunks_path index exists") {
    auto db = makeFullyMigratedDb();
    ASSERT_TRUE(indexExists(db, "idx_skill_chunks_path"));
}

TEST("migration_0028: skill_chunks insert and select roundtrip") {
    auto db = makeFullyMigratedDb();

    // Insert a context_nodes row first (skill_id is a FK to context_nodes).
    db.run(
        "INSERT INTO context_nodes(node_key, title, content, source_file, tier, tags) "
        "VALUES ('test-skill', 'Test Skill', 'body', 'test.md', 'skill', '[]')"
    );

    // Retrieve the id.
    std::string node_id_str;
    db.query(
        "SELECT id FROM context_nodes WHERE node_key='test-skill'",
        {},
        [&](const Row& row) {
            if (!row.empty()) node_id_str = row[0];
        }
    );
    ASSERT_FALSE(node_id_str.empty());

    // Insert a skill_chunk row.
    db.run(
        "INSERT INTO skill_chunks(skill_id, parent_path, heading, content, token_count) "
        "VALUES (?, 'skills/test', '## Section', 'chunk body', 42)",
        {node_id_str}
    );

    // Verify it can be read back.
    int count = 0;
    std::string heading_out;
    db.query(
        "SELECT heading, token_count FROM skill_chunks WHERE skill_id=?",
        {node_id_str},
        [&](const Row& row) {
            ++count;
            if (!row.empty()) heading_out = row[0];
        }
    );

    ASSERT_EQ(count, 1);
    ASSERT_EQ(heading_out, std::string("## Section"));
}

TEST("migration_0028: skill_chunks cascades on context_nodes delete") {
    auto db = makeFullyMigratedDb();

    db.run("PRAGMA foreign_keys=ON");

    db.run(
        "INSERT INTO context_nodes(node_key, title, content, source_file, tier, tags) "
        "VALUES ('cascade-skill', 'Cascade Skill', 'body', 'x.md', 'skill', '[]')"
    );

    std::string node_id_str;
    db.query(
        "SELECT id FROM context_nodes WHERE node_key='cascade-skill'",
        {},
        [&](const Row& row) {
            if (!row.empty()) node_id_str = row[0];
        }
    );
    ASSERT_FALSE(node_id_str.empty());

    db.run(
        "INSERT INTO skill_chunks(skill_id, parent_path, heading, content) "
        "VALUES (?, 'skills/x', '## X', 'x body')",
        {node_id_str}
    );

    // Delete the parent node — chunks should cascade-delete.
    db.run("DELETE FROM context_nodes WHERE id=?", {node_id_str});

    int remaining = 0;
    db.query(
        "SELECT COUNT(*) FROM skill_chunks WHERE skill_id=?",
        {node_id_str},
        [&](const Row& row) {
            if (!row.empty()) remaining = std::stoi(row[0]);
        }
    );
    ASSERT_EQ(remaining, 0);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
