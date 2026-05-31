// M7-A T1: auto-register project in global.db via icmg init.
// Tests GlobalDb project SQL behavior in isolation (same pattern as test_global_db.cpp).
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include <string>

static icmg::core::Db makeGlobalDb() {
    icmg::core::Db db(":memory:");
    db.run(
        "CREATE TABLE projects("
        "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name          TEXT NOT NULL UNIQUE,"
        "  path          TEXT NOT NULL,"
        "  db_path       TEXT NOT NULL,"
        "  description   TEXT DEFAULT '',"
        "  registered_at INTEGER DEFAULT (strftime('%s','now'))"
        ")");
    return db;
}

static bool projectExists(icmg::core::Db& db, const std::string& name) {
    bool found = false;
    db.query("SELECT 1 FROM projects WHERE name=?", {name},
        [&](const icmg::core::Row&) { found = true; });
    return found;
}

TEST("init_project_register: insert project registers in global.db") {
    auto db = makeGlobalDb();
    db.run("INSERT INTO projects(name,path,db_path,description) VALUES(?,?,?,?)",
           {"icemage-code",
            "D:/Data Kerja/Personal/AI/icemage-code",
            "D:/Data Kerja/Personal/AI/icemage-code/.icmg/data.db",
            "auto-registered by icmg init"});
    ASSERT_TRUE(projectExists(db, "icemage-code"));
}

TEST("init_project_register: duplicate name is idempotent via INSERT OR REPLACE") {
    auto db = makeGlobalDb();
    db.run("INSERT OR REPLACE INTO projects(name,path,db_path) VALUES(?,?,?)",
           {"proj", "/a/b", "/a/b/.icmg/data.db"});
    db.run("INSERT OR REPLACE INTO projects(name,path,db_path) VALUES(?,?,?)",
           {"proj", "/a/b", "/a/b/.icmg/data.db"});
    int cnt = 0;
    db.query("SELECT COUNT(*) FROM projects WHERE name='proj'", {},
        [&](const icmg::core::Row& r) { cnt = std::stoi(r[0]); });
    ASSERT_EQ(cnt, 1);
}

TEST("init_project_register: query returns registered path") {
    auto db = makeGlobalDb();
    db.run("INSERT INTO projects(name,path,db_path) VALUES(?,?,?)",
           {"test-proj", "/test/root", "/test/root/.icmg/data.db"});
    std::string got_path;
    db.query("SELECT path FROM projects WHERE name=?", {"test-proj"},
        [&](const icmg::core::Row& r) { got_path = r[0]; });
    ASSERT_EQ(got_path, std::string("/test/root"));
}
