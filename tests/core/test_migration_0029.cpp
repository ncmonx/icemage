// Test: migration 0029 creates focus_chain table + index.
// Verifies schema is applied correctly via the embedded SQL.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include <string>
#include <vector>

using namespace icmg::core;

static const char* kMigration0029 = R"SQL(
CREATE TABLE IF NOT EXISTS focus_chain (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  TEXT    NOT NULL,
    todo        TEXT    NOT NULL,
    status      TEXT    NOT NULL DEFAULT 'in' CHECK(status IN ('in','done','blocked')),
    ord         INTEGER NOT NULL,
    created_at  INTEGER DEFAULT (strftime('%s','now')),
    updated_at  INTEGER DEFAULT (strftime('%s','now'))
);
CREATE INDEX IF NOT EXISTS idx_focus_chain_session ON focus_chain(session_id, ord);
)SQL";

static Db makeDb() { return Db(":memory:"); }

TEST("migration_0029: focus_chain table exists after migration") {
    auto db = makeDb();
    db.run(kMigration0029);

    bool found = false;
    db.query(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='focus_chain'",
        {},
        [&](const Row& row) {
            if (!row.empty() && row[0] == "focus_chain") found = true;
        });
    ASSERT_TRUE(found);
}

TEST("migration_0029: focus_chain has expected columns") {
    auto db = makeDb();
    db.run(kMigration0029);

    // PRAGMA table_info returns rows: cid | name | type | notnull | dflt_value | pk
    std::vector<std::string> cols;
    db.query("PRAGMA table_info(focus_chain)", {}, [&](const Row& row) {
        if (row.size() > 1) cols.push_back(row[1]);
    });

    ASSERT_TRUE(!cols.empty());

    auto has = [&](const std::string& c) {
        for (auto& col : cols) if (col == c) return true;
        return false;
    };
    ASSERT_TRUE(has("id"));
    ASSERT_TRUE(has("session_id"));
    ASSERT_TRUE(has("todo"));
    ASSERT_TRUE(has("status"));
    ASSERT_TRUE(has("ord"));
    ASSERT_TRUE(has("created_at"));
    ASSERT_TRUE(has("updated_at"));
}

TEST("migration_0029: idx_focus_chain_session index exists") {
    auto db = makeDb();
    db.run(kMigration0029);

    bool found = false;
    db.query(
        "SELECT name FROM sqlite_master WHERE type='index' AND name='idx_focus_chain_session'",
        {},
        [&](const Row& row) {
            if (!row.empty() && row[0] == "idx_focus_chain_session") found = true;
        });
    ASSERT_TRUE(found);
}

TEST("migration_0029: focus_chain insert + select roundtrip") {
    auto db = makeDb();
    db.run(kMigration0029);

    db.run("INSERT INTO focus_chain(session_id, todo, status, ord) "
           "VALUES ('sess-1', 'Write tests', 'in', 1)");

    int count = 0;
    std::string todo_out;
    db.query("SELECT todo, status FROM focus_chain WHERE session_id='sess-1'",
             {},
             [&](const Row& row) {
                 ++count;
                 if (!row.empty()) todo_out = row[0];
             });
    ASSERT_EQ(count, 1);
    ASSERT_EQ(todo_out, std::string("Write tests"));
}

TEST("migration_0029: status CHECK constraint rejects invalid value") {
    auto db = makeDb();
    db.run(kMigration0029);

    bool threw = false;
    try {
        db.run("INSERT INTO focus_chain(session_id, todo, status, ord) "
               "VALUES ('s', 't', 'invalid_status', 1)");
    } catch (...) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

int main() { return icmg::test::run_all(); }
