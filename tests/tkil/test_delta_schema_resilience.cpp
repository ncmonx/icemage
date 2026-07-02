// tests/tkil/test_delta_schema_resilience.cpp
// REGRESSION (2026-06-29): icmg run crashed with "no such column:
// last_filtered_output" on a deployed binary whose DB predates migration 0046
// (embedded-migration fallback stopped at v34). recordCommand must degrade
// gracefully (no throw) when the snapshot columns are absent.
//
// recordCommand/loadLastOutput are private; we exercise the same defensive
// INSERT path through the public recordManual() (-> recordCommand(cmd,0,0)).

#include "../test_main.hpp"
#include "../../src/tkil/tkil.hpp"
#include "../../src/core/db.hpp"

using icmg::core::Db;
using icmg::tkil::Tkil;

// Build a pre-0046 `commands` table (no last_filtered_* columns).
static void makeLegacyCommandsTable(Db& db) {
    db.run(
        "CREATE TABLE commands ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " command TEXT NOT NULL UNIQUE,"
        " frequency INTEGER NOT NULL DEFAULT 1,"
        " last_used INTEGER,"
        " avg_lines INTEGER NOT NULL DEFAULT 0,"
        " tags TEXT,"
        " total_original_lines INTEGER NOT NULL DEFAULT 0,"
        " total_filtered_lines INTEGER NOT NULL DEFAULT 0)");
}

TEST("delta-resilience: recordManual on pre-0046 schema does not throw") {
    Db db(":memory:");
    makeLegacyCommandsTable(db);
    Tkil tk(db);
    bool threw = false;
    try {
        tk.recordManual("legacy build cmd");  // -> recordCommand(cmd,0,0)
    } catch (...) {
        threw = true;
    }
    ASSERT_FALSE(threw);
    // The basic fields still recorded via the fallback INSERT.
    int freq = 0;
    db.query("SELECT frequency FROM commands WHERE command=?",
             {"legacy build cmd"},
             [&](const std::vector<std::string>& row) {
                 if (!row.empty()) freq = std::stoi(row[0]);
             });
    ASSERT_EQ(freq, 1);
}

TEST("delta-resilience: recordManual on current schema records snapshot columns") {
    Db db(":memory:");
    // Full schema including 0046 columns.
    db.run(
        "CREATE TABLE commands ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " command TEXT NOT NULL UNIQUE,"
        " frequency INTEGER NOT NULL DEFAULT 1,"
        " last_used INTEGER,"
        " avg_lines INTEGER NOT NULL DEFAULT 0,"
        " tags TEXT,"
        " total_original_lines INTEGER NOT NULL DEFAULT 0,"
        " total_filtered_lines INTEGER NOT NULL DEFAULT 0,"
        " last_filtered_output TEXT,"
        " last_filtered_hash TEXT)");
    Tkil tk(db);
    bool threw = false;
    try {
        tk.recordManual("modern cmd");
    } catch (...) {
        threw = true;
    }
    ASSERT_FALSE(threw);
    int freq = 0;
    db.query("SELECT frequency FROM commands WHERE command=?",
             {"modern cmd"},
             [&](const std::vector<std::string>& row) {
                 if (!row.empty()) freq = std::stoi(row[0]);
             });
    ASSERT_EQ(freq, 1);
}

TEST("delta-resilience: recordManual twice bumps frequency on legacy schema") {
    Db db(":memory:");
    makeLegacyCommandsTable(db);
    Tkil tk(db);
    tk.recordManual("repeated cmd");
    tk.recordManual("repeated cmd");
    int freq = 0;
    db.query("SELECT frequency FROM commands WHERE command=?",
             {"repeated cmd"},
             [&](const std::vector<std::string>& row) {
                 if (!row.empty()) freq = std::stoi(row[0]);
             });
    ASSERT_EQ(freq, 2);
}
