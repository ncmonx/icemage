// tests/core/test_zone_resolver.cpp
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/zone_resolver.hpp"

using namespace icmg;

static core::Db makeDb() {
    core::Db db(":memory:");
    db.run(
        "CREATE TABLE graph_nodes("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " path TEXT NOT NULL UNIQUE,"
        " zone TEXT NOT NULL DEFAULT 'default'"
        ")");
    db.run(
        "CREATE TABLE zone_config("
        " zone TEXT PRIMARY KEY,"
        " description TEXT,"
        " path_glob TEXT,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")");
    db.run("INSERT INTO zone_config(zone, description) VALUES('default', 'Catch-all')", {});
    return db;
}

TEST("zone resolver: prefix glob matches /**") {
    auto db = makeDb();
    core::ZoneResolver z(db);
    z.addRule("api", "src/api/**");
    z.addRule("sync", "src/sync/**");

    ASSERT_EQ(z.resolveForPath("src/api/foo.cs"), std::string("api"));
    ASSERT_EQ(z.resolveForPath("src/sync/bar.cs"), std::string("sync"));
    ASSERT_EQ(z.resolveForPath("src/other/baz.cs"), std::string("default"));
}

TEST("zone resolver: backslash paths normalized") {
    auto db = makeDb();
    core::ZoneResolver z(db);
    z.addRule("ui", "src/ui/**");
    ASSERT_EQ(z.resolveForPath("src\\ui\\Form1.cs"), std::string("ui"));
}

TEST("zone resolver: windows absolute path matches relative glob") {
    auto db = makeDb();
    core::ZoneResolver z(db);
    z.addRule("cli", "src/cli/**");
    z.addRule("core", "src/core/**");
    // Absolute paths as stored on Windows — must resolve to correct zone
    ASSERT_EQ(z.resolveForPath("D:\\Data Kerja\\proj\\src\\cli\\commands\\foo.cpp"), std::string("cli"));
    ASSERT_EQ(z.resolveForPath("D:/Data Kerja/proj/src/core/db.cpp"), std::string("core"));
    ASSERT_EQ(z.resolveForPath("D:/proj/src/other/baz.cpp"), std::string("default"));
}

TEST("zone resolver: leading ./ stripped") {
    auto db = makeDb();
    core::ZoneResolver z(db);
    z.addRule("tests", "tests/**");
    ASSERT_EQ(z.resolveForPath("./tests/test_foo.cpp"), std::string("tests"));
    ASSERT_EQ(z.resolveForPath(".\\tests\\test_bar.cpp"), std::string("tests"));
}

TEST("zone resolver: longer globs win (specificity)") {
    auto db = makeDb();
    core::ZoneResolver z(db);
    z.addRule("payment", "src/api/payment/**");
    z.addRule("api", "src/api/**");

    ASSERT_EQ(z.resolveForPath("src/api/payment/checkout.cs"), std::string("payment"));
    ASSERT_EQ(z.resolveForPath("src/api/users.cs"), std::string("api"));
}

TEST("zone resolver: extension glob") {
    auto db = makeDb();
    core::ZoneResolver z(db);
    z.addRule("schema", "*.sql");
    ASSERT_EQ(z.resolveForPath("migrations/0001.sql"), std::string("schema"));
    ASSERT_EQ(z.resolveForPath("src/foo.cs"), std::string("default"));
}

TEST("zone resolver: rebuild updates existing rows") {
    auto db = makeDb();
    db.run("INSERT INTO graph_nodes(path, zone) VALUES('src/api/x.cs', 'default')", {});
    db.run("INSERT INTO graph_nodes(path, zone) VALUES('src/sync/y.cs', 'default')", {});
    core::ZoneResolver z(db);
    z.addRule("api", "src/api/**");
    z.addRule("sync", "src/sync/**");
    int n = z.rebuild();
    ASSERT_EQ(n, 2);

    std::string z1, z2;
    db.query("SELECT zone FROM graph_nodes WHERE path = 'src/api/x.cs'", {},
             [&](const core::Row& r) { if (!r.empty()) z1 = r[0]; });
    db.query("SELECT zone FROM graph_nodes WHERE path = 'src/sync/y.cs'", {},
             [&](const core::Row& r) { if (!r.empty()) z2 = r[0]; });
    ASSERT_EQ(z1, std::string("api"));
    ASSERT_EQ(z2, std::string("sync"));
}

TEST("zone resolver: assign bulk re-tags matching paths only") {
    auto db = makeDb();
    db.run("INSERT INTO graph_nodes(path, zone) VALUES('src/legacy/a.cs', 'default')", {});
    db.run("INSERT INTO graph_nodes(path, zone) VALUES('src/legacy/b.cs', 'default')", {});
    db.run("INSERT INTO graph_nodes(path, zone) VALUES('src/new/c.cs', 'default')", {});
    core::ZoneResolver z(db);
    int n = z.assign("src/legacy/**", "legacy");
    ASSERT_EQ(n, 2);

    int legacy_count = 0;
    db.query("SELECT COUNT(*) FROM graph_nodes WHERE zone = 'legacy'", {},
             [&](const core::Row& r) { if (!r.empty()) try { legacy_count = std::stoi(r[0]); } catch(...){} });
    ASSERT_EQ(legacy_count, 2);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
