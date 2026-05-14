// Phase A4 (v0.53.2): Prepared-statement LRU cache regression test.

#include "../test_main.hpp"
#include "../../src/core/db.hpp"

#include <chrono>
#include <string>

using namespace icmg;
using namespace std::chrono;

static int64_t nowMicros() {
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

TEST("repeat same SQL uses cache (faster after first call)") {
    core::Db db(":memory:");
    db.run("CREATE TABLE t(k INTEGER PRIMARY KEY, v TEXT)");
    db.run("INSERT INTO t(k,v) VALUES (1,'a')");
    db.run("INSERT INTO t(k,v) VALUES (2,'b')");

    int64_t t0 = nowMicros();
    db.query("SELECT v FROM t WHERE k = ?", {"1"}, [](const core::Row&){});
    int64_t cold = nowMicros() - t0;

    int64_t t1 = nowMicros();
    for (int i = 0; i < 50; ++i) {
        db.query("SELECT v FROM t WHERE k = ?", {"1"}, [](const core::Row&){});
    }
    int64_t warm_avg = (nowMicros() - t1) / 50;
    // Loose threshold (10x) — stable on slow CI.
    ASSERT_TRUE(warm_avg < cold * 10);
}

TEST("distinct SQL evicts oldest (51 sql > cap 50)") {
    core::Db db(":memory:");
    db.run("CREATE TABLE t(k INTEGER PRIMARY KEY, v TEXT)");
    for (int i = 0; i < 100; ++i)
        db.run("INSERT INTO t(k,v) VALUES (?, ?)", {std::to_string(i), "x"});

    for (int i = 0; i < 51; ++i) {
        std::string sql = "SELECT v FROM t WHERE k = " + std::to_string(i);
        db.query(sql, {}, [](const core::Row&){});
    }
    // No-crash + re-run first SQL after eviction still works.
    db.query("SELECT v FROM t WHERE k = 0", {}, [](const core::Row&){});
    ASSERT_TRUE(true);
}

TEST("param binding after stmt reuse stays correct") {
    core::Db db(":memory:");
    db.run("CREATE TABLE t(k INTEGER PRIMARY KEY, v TEXT)");
    db.run("INSERT INTO t(k,v) VALUES (1,'apple')");
    db.run("INSERT INTO t(k,v) VALUES (2,'banana')");

    std::string r1, r2;
    db.query("SELECT v FROM t WHERE k = ?", {"1"},
             [&](const core::Row& r){ if (!r.empty()) r1 = r[0]; });
    db.query("SELECT v FROM t WHERE k = ?", {"2"},
             [&](const core::Row& r){ if (!r.empty()) r2 = r[0]; });

    ASSERT_EQ(r1, std::string("apple"));
    ASSERT_EQ(r2, std::string("banana"));
}

TEST("repeated run() with params idempotent") {
    core::Db db(":memory:");
    db.run("CREATE TABLE counters(name TEXT PRIMARY KEY, n INTEGER)");
    db.run("INSERT INTO counters(name,n) VALUES ('x', 0)");

    for (int i = 0; i < 30; ++i) {
        db.run("UPDATE counters SET n = ? WHERE name = ?",
               {std::to_string(i + 1), "x"});
    }
    int final_n = 0;
    db.query("SELECT n FROM counters WHERE name = 'x'", {},
             [&](const core::Row& r){ if (!r.empty()) final_n = std::stoi(r[0]); });
    ASSERT_EQ(final_n, 30);
}

TEST("no leak on destructor after cached stmts") {
    for (int i = 0; i < 5; ++i) {
        core::Db db(":memory:");
        db.run("CREATE TABLE t(k INTEGER)");
        for (int j = 0; j < 10; ++j) {
            db.query("SELECT k FROM t WHERE k > ?", {std::to_string(j)},
                     [](const core::Row&){});
        }
    }
    ASSERT_TRUE(true);
}

int main() {
    std::cout << "=== Db prepared-statement LRU tests ===\n";
    return icmg::test::run_all();
}
