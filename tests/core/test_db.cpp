#include "../test_main.hpp"
#include "../../src/core/db.hpp"

// ---- core::Db unit tests (in-memory SQLite) --------------------------------

TEST("db: open in-memory + create table") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE t(id INTEGER PRIMARY KEY, val TEXT)");
    // No throw = pass
    ASSERT_TRUE(true);
}

TEST("db: insert + query round-trip") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE t(id INTEGER PRIMARY KEY, val TEXT)");
    db.run("INSERT INTO t(val) VALUES(?)", {"hello"});

    std::string got;
    db.query("SELECT val FROM t", {}, [&](const icmg::core::Row& r) {
        if (!r.empty()) got = r[0];
    });
    ASSERT_EQ(got, std::string("hello"));
}

TEST("db: upsert — ON CONFLICT DO UPDATE") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE kv(key TEXT PRIMARY KEY, cnt INTEGER NOT NULL DEFAULT 0)");
    db.run("INSERT INTO kv(key,cnt) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET cnt=cnt+?",
           {"foo", "1", "1"});
    db.run("INSERT INTO kv(key,cnt) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET cnt=cnt+?",
           {"foo", "1", "1"});

    int cnt = 0;
    db.query("SELECT cnt FROM kv WHERE key='foo'", {}, [&](const icmg::core::Row& r) {
        if (!r.empty()) try { cnt = std::stoi(r[0]); } catch (...) {}
    });
    ASSERT_EQ(cnt, 2);
}

TEST("db: query empty table returns no rows") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE t(id INTEGER PRIMARY KEY)");
    int count = 0;
    db.query("SELECT id FROM t", {}, [&](const icmg::core::Row&) { ++count; });
    ASSERT_EQ(count, 0);
}

TEST("db: multiple rows returned in order") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER)");
    db.run("INSERT INTO t(v) VALUES(?)", {"10"});
    db.run("INSERT INTO t(v) VALUES(?)", {"20"});
    db.run("INSERT INTO t(v) VALUES(?)", {"30"});

    std::vector<int> vals;
    db.query("SELECT v FROM t ORDER BY v", {}, [&](const icmg::core::Row& r) {
        if (!r.empty()) try { vals.push_back(std::stoi(r[0])); } catch (...) {}
    });
    ASSERT_EQ(vals.size(), 3u);
    ASSERT_EQ(vals[0], 10);
    ASSERT_EQ(vals[2], 30);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
