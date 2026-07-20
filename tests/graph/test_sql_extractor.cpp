// tests/graph/test_sql_extractor.cpp
//
// TDD tests for SqlExtractor (gap G3 vs graphify: SQL schema graph).
//
// Encoding conventions:
//   tables    << table name          -> CREATE TABLE <name>
//   functions << routine name         -> CREATE FUNCTION/PROCEDURE/VIEW <name>
//   imports   << "references:<table>"  -> FK/REFERENCES edge to another table

#include "../test_main.hpp"
#include "../../src/graph/extractor/base_extractor.hpp"
#include "../../src/core/registry.hpp"
#include <string>
#include <algorithm>

static icmg::graph::BaseExtractor* getSqlExt() {
    static auto inst = []() -> std::unique_ptr<icmg::graph::BaseExtractor> {
        auto& reg = icmg::core::Registry<icmg::graph::BaseExtractor>::instance();
        if (!reg.has("sql")) return nullptr;
        return reg.create("sql");
    }();
    return inst.get();
}

static bool has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

TEST("sql-extractor: registered") {
    ASSERT_TRUE(getSqlExt() != nullptr);
}

TEST("sql-extractor: CREATE TABLE -> tables list") {
    auto* ext = getSqlExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "CREATE TABLE users (\n"
        "  id INTEGER PRIMARY KEY,\n"
        "  name TEXT NOT NULL\n"
        ");\n";
    auto r = ext->extract("schema.sql", src);
    ASSERT_TRUE(has(r.tables, "users"));
}

TEST("sql-extractor: multiple tables, IF NOT EXISTS, quoted names") {
    auto* ext = getSqlExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "CREATE TABLE IF NOT EXISTS orders (id INT);\n"
        "CREATE TABLE \"line_items\" (id INT);\n"
        "create table `products` (id int);\n";
    auto r = ext->extract("schema.sql", src);
    ASSERT_TRUE(has(r.tables, "orders"));
    ASSERT_TRUE(has(r.tables, "line_items"));
    ASSERT_TRUE(has(r.tables, "products"));
}

TEST("sql-extractor: schema-qualified name keeps table part") {
    auto* ext = getSqlExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src = "CREATE TABLE public.customers (id INT);\n";
    auto r = ext->extract("schema.sql", src);
    ASSERT_TRUE(has(r.tables, "customers"));
}

TEST("sql-extractor: FOREIGN KEY REFERENCES -> references import") {
    auto* ext = getSqlExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "CREATE TABLE orders (\n"
        "  id INTEGER PRIMARY KEY,\n"
        "  user_id INTEGER REFERENCES users(id)\n"
        ");\n";
    auto r = ext->extract("schema.sql", src);
    ASSERT_TRUE(has(r.tables, "orders"));
    ASSERT_TRUE(has(r.imports, "references:users"));
}

TEST("sql-extractor: explicit FOREIGN KEY ... REFERENCES clause") {
    auto* ext = getSqlExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "CREATE TABLE line_items (\n"
        "  id INT,\n"
        "  order_id INT,\n"
        "  FOREIGN KEY (order_id) REFERENCES orders (id)\n"
        ");\n";
    auto r = ext->extract("schema.sql", src);
    ASSERT_TRUE(has(r.imports, "references:orders"));
}

TEST("sql-extractor: CREATE VIEW / FUNCTION -> functions list") {
    auto* ext = getSqlExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "CREATE VIEW active_users AS SELECT * FROM users;\n"
        "CREATE FUNCTION total(x INT) RETURNS INT AS $$ SELECT x $$;\n";
    auto r = ext->extract("schema.sql", src);
    ASSERT_TRUE(has(r.functions, "active_users"));
    ASSERT_TRUE(has(r.functions, "total"));
}

TEST("sql-extractor: line and block comments ignored") {
    auto* ext = getSqlExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "-- CREATE TABLE commented_out (id INT);\n"
        "/* CREATE TABLE also_out (id INT); */\n"
        "CREATE TABLE real_table (id INT);\n";
    auto r = ext->extract("schema.sql", src);
    ASSERT_TRUE(has(r.tables, "real_table"));
    ASSERT_TRUE(!has(r.tables, "commented_out"));
    ASSERT_TRUE(!has(r.tables, "also_out"));
}

TEST("sql-extractor: extensions include .sql") {
    auto* ext = getSqlExt();
    ASSERT_TRUE(ext != nullptr);
    ASSERT_TRUE(has(ext->extensions(), ".sql"));
}

TEST("sql-extractor: no crash on empty / junk input") {
    auto* ext = getSqlExt();
    ASSERT_TRUE(ext != nullptr);
    auto r1 = ext->extract("e.sql", "");
    auto r2 = ext->extract("e.sql", "not really sql ;;; ((( ");
    (void)r1; (void)r2;
}
