#include "../test_main.hpp"
#include "../../src/core/db.hpp"

// Test GlobalDb logic in isolation using an in-memory Db.
// We don't test GlobalDb singleton (needs filesystem) — test the SQL behavior.

static icmg::core::Db makeGlobalDb() {
    icmg::core::Db db(":memory:");
    db.run(
        "CREATE TABLE projects("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT NOT NULL UNIQUE,"
        " path TEXT NOT NULL,"
        " db_path TEXT NOT NULL,"
        " description TEXT NOT NULL DEFAULT '',"
        " registered_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );
    return db;
}

TEST("global_db: insert + query project") {
    auto db = makeGlobalDb();
    db.run("INSERT INTO projects(name,path,db_path) VALUES(?,?,?)",
           {"myproj", "/home/user/myproj", "/home/user/myproj/.icmg/data.db"});

    std::string name;
    db.query("SELECT name FROM projects WHERE name=?", {"myproj"},
             [&](const icmg::core::Row& r) { if (!r.empty()) name = r[0]; });

    ASSERT_EQ(name, std::string("myproj"));
}

TEST("global_db: unique name constraint") {
    auto db = makeGlobalDb();
    db.run("INSERT INTO projects(name,path,db_path) VALUES(?,?,?)",
           {"p1", "/a", "/a/.icmg/data.db"});

    bool threw = false;
    try {
        db.run("INSERT INTO projects(name,path,db_path) VALUES(?,?,?)",
               {"p1", "/b", "/b/.icmg/data.db"});
    } catch (...) { threw = true; }

    ASSERT_TRUE(threw);
}

TEST("global_db: list projects ordered by name") {
    auto db = makeGlobalDb();
    db.run("INSERT INTO projects(name,path,db_path) VALUES(?,?,?)",
           {"zebra", "/z", "/z/.icmg/data.db"});
    db.run("INSERT INTO projects(name,path,db_path) VALUES(?,?,?)",
           {"alpha", "/a", "/a/.icmg/data.db"});

    std::vector<std::string> names;
    db.query("SELECT name FROM projects ORDER BY name", {},
             [&](const icmg::core::Row& r) { if (!r.empty()) names.push_back(r[0]); });

    ASSERT_EQ(names.size(), 2u);
    ASSERT_EQ(names[0], std::string("alpha"));
    ASSERT_EQ(names[1], std::string("zebra"));
}

TEST("global_db: delete project") {
    auto db = makeGlobalDb();
    db.run("INSERT INTO projects(name,path,db_path) VALUES(?,?,?)",
           {"p1", "/a", "/a/.icmg/data.db"});
    db.run("DELETE FROM projects WHERE name=?", {"p1"});

    int count = 0;
    db.query("SELECT COUNT(*) FROM projects", {},
             [&](const icmg::core::Row& r) {
                 if (!r.empty()) try { count = std::stoi(r[0]); } catch (...) {}
             });
    ASSERT_EQ(count, 0);
}

TEST("config: projectDbPath uses override when set") {
    auto& cfg = icmg::core::Config::instance();
    cfg.setProjectDbOverride("/custom/path/data.db");
    ASSERT_EQ(cfg.projectDbPath("."), std::string("/custom/path/data.db"));
    cfg.clearProjectDbOverride();
    // After clear: uses CWD-derived path, not override
    ASSERT_TRUE(cfg.projectDbPath(".").find("data.db") != std::string::npos);
}

int main() {
    std::cout << "=== GlobalDb / ProjectContext tests ===\n";
    return icmg::test::run_all();
}
