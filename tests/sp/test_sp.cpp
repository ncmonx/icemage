#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/sp/sp_store.hpp"
#include "../../src/sp/sql_parser.hpp"
#include <algorithm>

using namespace icmg;

// ---------------------------------------------------------------------------
// Helper: in-memory DB with SP schema
// ---------------------------------------------------------------------------

static core::Db makeDb() {
    core::Db db(":memory:");
    db.run(
        "CREATE TABLE stored_procedures("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT NOT NULL,"
        " db_type TEXT,"
        " database_name TEXT,"
        " content TEXT NOT NULL,"
        " context TEXT,"
        " parameters TEXT,"
        " return_type TEXT,"
        " tables_used TEXT,"
        " sp_dependencies TEXT,"
        " scope_path TEXT,"
        " tags TEXT,"
        " version INTEGER NOT NULL DEFAULT 1,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " UNIQUE(name, database_name)"
        ")"
    );
    db.run(
        "CREATE TABLE sp_versions("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " sp_id INTEGER NOT NULL REFERENCES stored_procedures(id) ON DELETE CASCADE,"
        " version INTEGER NOT NULL,"
        " content TEXT NOT NULL,"
        " change_note TEXT,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );
    return db;
}

static sp::StoredProcedure makeSp(const std::string& name,
                                   const std::string& sql = "SELECT 1") {
    sp::StoredProcedure s;
    s.name = name;
    s.db_type = "mssql";
    s.database_name = "testdb";
    s.content = sql;
    s.context = "Test SP";
    return s;
}

// ---------------------------------------------------------------------------
// SQL Parser tests
// ---------------------------------------------------------------------------

TEST("sql_parser: detect mssql from @ params") {
    sp::SqlParser parser;
    auto r = parser.parse("CREATE PROCEDURE sp_Test @id INT AS BEGIN SELECT 1 END");
    ASSERT_EQ(r.db_type, std::string("mssql"));
}

TEST("sql_parser: detect postgresql from CREATE OR REPLACE") {
    sp::SqlParser parser;
    auto r = parser.parse("CREATE OR REPLACE PROCEDURE sp_Test() AS $$ BEGIN END $$ LANGUAGE plpgsql");
    ASSERT_EQ(r.db_type, std::string("postgresql"));
}

TEST("sql_parser: extract SP name") {
    sp::SqlParser parser;
    auto r = parser.parse("CREATE PROCEDURE sp_GetBKM @id INT AS BEGIN SELECT 1 END");
    ASSERT_EQ(r.sp_name, std::string("sp_GetBKM"));
}

TEST("sql_parser: extract description comment") {
    sp::SqlParser parser;
    std::string sql = "-- Description: Get BKM by date\nCREATE PROCEDURE sp_GetBKM AS BEGIN END";
    auto r = parser.parse(sql);
    ASSERT_EQ(r.context, std::string("Get BKM by date"));
}

TEST("sql_parser: extract MSSQL params") {
    sp::SqlParser parser;
    auto r = parser.parse("CREATE PROCEDURE sp_Test @id INT, @name VARCHAR(50) AS BEGIN END");
    ASSERT_EQ(r.parameters.size(), 2u);
    ASSERT_EQ(r.parameters[0].name, std::string("@id"));
    ASSERT_EQ(r.parameters[0].type, std::string("INT"));
    ASSERT_EQ(r.parameters[1].name, std::string("@name"));
}

TEST("sql_parser: extract tables") {
    sp::SqlParser parser;
    auto r = parser.parse(
        "CREATE PROCEDURE sp_T AS BEGIN SELECT * FROM orders JOIN customers ON 1=1 END",
        "mssql");
    ASSERT_TRUE(std::find(r.tables.begin(), r.tables.end(), "orders") != r.tables.end());
    ASSERT_TRUE(std::find(r.tables.begin(), r.tables.end(), "customers") != r.tables.end());
}

TEST("sql_parser: strip string literals - no false table extraction") {
    sp::SqlParser parser;
    // 'FROM fake_table' in a string should NOT be extracted
    auto r = parser.parse(
        "CREATE PROCEDURE sp_T AS BEGIN "
        "SELECT * FROM real_table WHERE col = 'FROM fake_table' END",
        "mssql");
    ASSERT_TRUE(std::find(r.tables.begin(), r.tables.end(), "real_table") != r.tables.end());
    ASSERT_TRUE(std::find(r.tables.begin(), r.tables.end(), "fake_table") == r.tables.end());
}

TEST("sql_parser: extract MSSQL sp calls") {
    sp::SqlParser parser;
    auto r = parser.parse(
        "CREATE PROCEDURE sp_Main AS BEGIN EXEC sp_Sub1 @id EXEC sp_Sub2 END",
        "mssql");
    ASSERT_TRUE(std::find(r.sp_calls.begin(), r.sp_calls.end(), "sp_sub1") != r.sp_calls.end());
    ASSERT_TRUE(std::find(r.sp_calls.begin(), r.sp_calls.end(), "sp_sub2") != r.sp_calls.end());
}

TEST("sql_parser: no self-reference in sp_calls") {
    sp::SqlParser parser;
    // sp_Main calling itself should be removed from calls
    auto r = parser.parse(
        "CREATE PROCEDURE sp_Main AS BEGIN EXEC sp_Main EXEC sp_Other END",
        "mssql");
    // sp_main (lowercased) should not appear in calls
    std::string lo_self = "sp_main";
    ASSERT_TRUE(std::find(r.sp_calls.begin(), r.sp_calls.end(), lo_self) == r.sp_calls.end());
}

// ---------------------------------------------------------------------------
// SpStore tests
// ---------------------------------------------------------------------------

TEST("sp_store: add + get") {
    auto db = makeDb();
    sp::SpStore store(db);
    auto s = makeSp("sp_GetBKM");
    s.tables_used = {"bukti_kas_masuk", "cabang"};

    int64_t id = store.add(s);
    ASSERT_TRUE(id > 0);

    auto got = store.get("sp_GetBKM");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->name, std::string("sp_GetBKM"));
    ASSERT_EQ(got->tables_used.size(), 2u);
}

TEST("sp_store: duplicate add bumps version") {
    auto db = makeDb();
    sp::SpStore store(db);

    auto s = makeSp("sp_Test", "SELECT 1");
    store.add(s);

    s.content = "SELECT 2";
    store.add(s);

    auto got = store.get("sp_Test");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->version, 2);
    ASSERT_EQ(got->content, std::string("SELECT 2"));
}

TEST("sp_store: history tracks old version") {
    auto db = makeDb();
    sp::SpStore store(db);

    auto s = makeSp("sp_Test", "SELECT 1");
    store.add(s);
    s.content = "SELECT 2";
    store.add(s);

    auto hist = store.history("sp_Test");
    ASSERT_EQ(hist.size(), 1u);
    ASSERT_EQ(hist[0].version, 1);
    ASSERT_EQ(hist[0].content, std::string("SELECT 1"));
}

TEST("sp_store: search by name") {
    auto db = makeDb();
    sp::SpStore store(db);
    store.add(makeSp("sp_GetBKM"));
    store.add(makeSp("sp_GetBKK"));
    store.add(makeSp("sp_Report"));

    auto res = store.search("BKM");
    ASSERT_EQ(res.size(), 1u);
    ASSERT_EQ(res[0].name, std::string("sp_GetBKM"));
}

TEST("sp_store: uses-table") {
    auto db = makeDb();
    sp::SpStore store(db);

    auto s = makeSp("sp_GetBKM");
    s.tables_used = {"bukti_kas_masuk"};
    store.add(s);

    auto res = store.usesTable("bukti_kas_masuk");
    ASSERT_EQ(res.size(), 1u);
    ASSERT_EQ(res[0].name, std::string("sp_GetBKM"));
}

TEST("sp_store: called-by") {
    auto db = makeDb();
    sp::SpStore store(db);

    auto parent = makeSp("sp_Main");
    parent.sp_dependencies = {"sp_Sub"};
    store.add(parent);

    auto res = store.calledBy("sp_Sub");
    ASSERT_EQ(res.size(), 1u);
    ASSERT_EQ(res[0].name, std::string("sp_Main"));
}

TEST("sp_store: remove") {
    auto db = makeDb();
    sp::SpStore store(db);
    store.add(makeSp("sp_ToRemove"));

    bool ok = store.remove("sp_ToRemove");
    ASSERT_TRUE(ok);

    auto got = store.get("sp_ToRemove");
    ASSERT_TRUE(!got.has_value());
}

TEST("sp_store: list filtered by db_type") {
    auto db = makeDb();
    sp::SpStore store(db);

    auto m = makeSp("sp_MSSQL"); m.db_type = "mssql";
    auto p = makeSp("sp_PG");    p.db_type = "postgresql"; p.database_name = "pgdb";
    store.add(m); store.add(p);

    auto all = store.list();
    ASSERT_EQ(all.size(), 2u);

    auto mssql_only = store.list("mssql");
    ASSERT_EQ(mssql_only.size(), 1u);
}

int main() {
    std::cout << "=== Stored Procedure Engine tests ===\n";
    return icmg::test::run_all();
}
