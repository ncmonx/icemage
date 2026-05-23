// SQL symbol extractor — tables, views, SPs, table refs (Phase 27 follow-up).
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"
#include <algorithm>

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

static const Symbol* find(const std::vector<Symbol>& syms, const std::string& kind, const std::string& name) {
    for (auto& s : syms) if (s.kind == kind && s.name == name) return &s;
    return nullptr;
}

TEST("sql: CREATE TABLE detected as kind=table") {
    auto e = Reg::instance().create("sql");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "CREATE TABLE [dbo].[Customer] (\n"
        "    Id INT PRIMARY KEY,\n"
        "    Name NVARCHAR(100)\n"
        ");\n"
        "GO\n";
    auto syms = e->extractSymbols("schema.sql", src);
    auto* t = find(syms, "table", "Customer");
    ASSERT_TRUE(t != nullptr);
    ASSERT_EQ(t->line_start, 1);
}

TEST("sql: bracketless table name") {
    auto e = Reg::instance().create("sql");
    auto syms = e->extractSymbols("s.sql", "CREATE TABLE OrderItem ( Id INT );");
    ASSERT_TRUE(find(syms, "table", "OrderItem") != nullptr);
}

TEST("sql: SP detects table refs in body (FROM/JOIN/UPDATE/INSERT INTO/DELETE FROM)") {
    auto e = Reg::instance().create("sql");
    std::string src =
        "CREATE PROC P AS\n"
        "BEGIN\n"
        "  SELECT * FROM [dbo].[Customer]\n"
        "  INNER JOIN OrderItem oi ON oi.OrderId = 1\n"
        "  UPDATE Audit SET ts = GETDATE()\n"
        "  INSERT INTO Log (msg) VALUES('hi')\n"
        "  DELETE FROM Stale\n"
        "END\n"
        "GO\n";
    auto syms = e->extractSymbols("p.sql", src);
    auto* p = find(syms, "sp", "P");
    ASSERT_TRUE(p != nullptr);
    auto has = [&](const std::string& n){
        return std::find(p->calls.begin(), p->calls.end(), n) != p->calls.end();
    };
    ASSERT_TRUE(has("Customer"));
    ASSERT_TRUE(has("OrderItem"));
    ASSERT_TRUE(has("Audit"));
    ASSERT_TRUE(has("Log"));
    ASSERT_TRUE(has("Stale"));
}

TEST("sql: SP refs filter SQL noise keywords") {
    auto e = Reg::instance().create("sql");
    std::string src =
        "CREATE PROC P AS\n"
        "BEGIN\n"
        "  SELECT * FROM Customer WHERE Id = 1\n"
        "END\n"
        "GO\n";
    auto syms = e->extractSymbols("p.sql", src);
    auto* p = find(syms, "sp", "P");
    ASSERT_TRUE(p != nullptr);
    auto has = [&](const std::string& n){
        return std::find(p->calls.begin(), p->calls.end(), n) != p->calls.end();
    };
    ASSERT_TRUE(has("Customer"));
    ASSERT_FALSE(has("WHERE"));
    ASSERT_FALSE(has("SELECT"));
}

TEST("sql: CREATE VIEW detected as kind=view + tables in body") {
    auto e = Reg::instance().create("sql");
    std::string src =
        "CREATE VIEW vSummary AS\n"
        "SELECT c.Id FROM Customer c\n"
        "LEFT JOIN OrderItem oi ON oi.OrderId = c.Id\n"
        "GO\n";
    auto syms = e->extractSymbols("v.sql", src);
    auto* v = find(syms, "view", "vSummary");
    ASSERT_TRUE(v != nullptr);
    auto has = [&](const std::string& n){
        return std::find(v->calls.begin(), v->calls.end(), n) != v->calls.end();
    };
    ASSERT_TRUE(has("Customer"));
    ASSERT_TRUE(has("OrderItem"));
}

TEST("sql: CREATE OR ALTER VIEW supported") {
    auto e = Reg::instance().create("sql");
    auto syms = e->extractSymbols("v.sql",
        "CREATE OR ALTER VIEW vX AS SELECT 1 FROM T GO");
    ASSERT_TRUE(find(syms, "view", "vX") != nullptr);
}

TEST("sql: deduped table refs (mentioned twice)") {
    auto e = Reg::instance().create("sql");
    std::string src =
        "CREATE PROC P AS\n"
        "  SELECT * FROM Customer\n"
        "  UPDATE Customer SET x = 1\n"
        "GO";
    auto syms = e->extractSymbols("p.sql", src);
    auto* p = find(syms, "sp", "P");
    ASSERT_TRUE(p != nullptr);
    int n = 0;
    for (auto& c : p->calls) if (c == "Customer") ++n;
    ASSERT_EQ(n, 1);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
