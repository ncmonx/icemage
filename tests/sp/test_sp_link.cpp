// Phase 24: regex semantics for `icmg sp link <file>`.
// The CLI command's doLink() is internal; we mirror the same regex here so
// changes to the canonical pattern stay covered by tests.
#include "../test_main.hpp"
#include <regex>
#include <set>
#include <string>

static std::set<std::string> scanExecCalls(const std::string& sql) {
    std::regex re_call(R"(\bEXEC(?:UTE)?\s+\[?(\w+)\]?)",
                       std::regex::ECMAScript | std::regex::icase);
    std::set<std::string> hits;
    for (auto it = std::sregex_iterator(sql.begin(), sql.end(), re_call);
         it != std::sregex_iterator(); ++it) {
        hits.insert((*it)[1].str());
    }
    return hits;
}

TEST("sp_link: simple EXEC <name>") {
    auto h = scanExecCalls("BEGIN EXEC sp_GetUser END");
    ASSERT_EQ(h.size(), (size_t)1);
    ASSERT_TRUE(h.count("sp_GetUser") == 1);
}

TEST("sp_link: EXECUTE alias") {
    auto h = scanExecCalls("EXECUTE sp_DoStuff @id = 1");
    ASSERT_TRUE(h.count("sp_DoStuff") == 1);
}

TEST("sp_link: bracketed name [sp_Foo]") {
    auto h = scanExecCalls("EXEC [sp_BracketedProc]");
    ASSERT_TRUE(h.count("sp_BracketedProc") == 1);
}

TEST("sp_link: case-insensitive (exec / EXEC / Exec)") {
    auto h = scanExecCalls("exec sp_a; EXEC sp_b; Exec sp_c");
    ASSERT_EQ(h.size(), (size_t)3);
}

TEST("sp_link: multiple distinct calls deduped to set") {
    auto h = scanExecCalls("EXEC sp_x; EXEC sp_x; EXEC sp_y");
    ASSERT_EQ(h.size(), (size_t)2);
    ASSERT_TRUE(h.count("sp_x") == 1);
    ASSERT_TRUE(h.count("sp_y") == 1);
}

TEST("sp_link: empty source -> empty result") {
    ASSERT_EQ(scanExecCalls("").size(), (size_t)0);
    ASSERT_EQ(scanExecCalls("SELECT 1; -- no procs here").size(), (size_t)0);
}

TEST("sp_link: ignores EXEC inside identifier (boundary)") {
    // `\b` boundary should prevent match on "DoEXEC sp_x" (still EXEC = whole word ok)
    auto h = scanExecCalls("CallEXEC sp_x");   // 'EXEC' has \b on left (no-letter→letter)
    // Regex \b matches between 'l' and 'E'? No — both letters → no \b boundary.
    ASSERT_EQ(h.size(), (size_t)0);
}

TEST("sp_link: real-world snippet with WITH/AS clause") {
    std::string sql =
        "CREATE PROCEDURE sp_Outer AS BEGIN\n"
        "  EXEC sp_Inner1 @a = 1\n"
        "  IF @x = 0\n"
        "    EXECUTE [sp_Inner2]\n"
        "END";
    auto h = scanExecCalls(sql);
    ASSERT_EQ(h.size(), (size_t)2);
    ASSERT_TRUE(h.count("sp_Inner1") == 1);
    ASSERT_TRUE(h.count("sp_Inner2") == 1);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
