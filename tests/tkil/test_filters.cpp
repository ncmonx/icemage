#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/tkil/base_filter.hpp"

// Filters are registered via static init from their .cpp TUs (linked via icmg_lib).
// Access them through the Registry.

using icmg::tkil::BaseFilter;
using Reg = icmg::core::Registry<icmg::tkil::BaseFilter>;

static BaseFilter* getFilter(const std::string& key) {
    static std::unordered_map<std::string, std::unique_ptr<BaseFilter>> cache;
    if (cache.find(key) == cache.end())
        cache[key] = Reg::instance().create(key);
    return cache[key].get();
}

// ---- Default filter --------------------------------------------------------

TEST("default filter: short output — no truncation") {
    auto* f = getFilter("default");
    std::string raw;
    for (int i = 0; i < 10; ++i) raw += "line " + std::to_string(i) + "\n";
    auto r = f->filter(raw, "ls");
    ASSERT_EQ(r.original_lines, 10);
    ASSERT_EQ(r.filtered_lines, 10);
    ASSERT_FALSE(r.was_truncated);
}

TEST("default filter: long output — head+tail kept") {
    auto* f = getFilter("default");
    std::string raw;
    for (int i = 0; i < 200; ++i) raw += "line " + std::to_string(i) + "\n";
    auto r = f->filter(raw, "ls");
    ASSERT_EQ(r.original_lines, 200);
    ASSERT_TRUE(r.was_truncated);
    ASSERT_CONTAINS(r.output, "line 0");       // head present
    ASSERT_CONTAINS(r.output, "line 199");     // tail present
    ASSERT_CONTAINS(r.output, "lines omitted");
}

// ---- Build filter ----------------------------------------------------------

TEST("build filter: keeps errors") {
    auto* f = getFilter("build");
    std::string raw =
        "Compiling foo.cpp\n"
        "error: use of undeclared identifier 'bar'\n"
        "Compiling baz.cpp\n"
        "build failed\n";
    auto r = f->filter(raw, "make");
    ASSERT_CONTAINS(r.output, "error:");
    ASSERT_NOT_CONTAINS(r.output, "Compiling");
}

TEST("build filter: keeps warnings") {
    auto* f = getFilter("build");
    std::string raw =
        "Compiling foo.cpp\n"
        "warning: unused variable 'x'\n"
        "build successful\n";
    auto r = f->filter(raw, "cargo build");
    ASSERT_CONTAINS(r.output, "warning:");
}

TEST("build filter: clean build — shows summary") {
    auto* f = getFilter("build");
    std::string raw =
        "Compiling foo.cpp\n"
        "Compiling bar.cpp\n"
        "Fresh icmg\n"
        "0 errors, 0 warnings\n";
    auto r = f->filter(raw, "cargo build");
    // Last line should be present (summary)
    ASSERT_CONTAINS(r.output, "0 errors");
}

// ---- Test filter -----------------------------------------------------------

TEST("test filter: keeps failures") {
    auto* f = getFilter("test");
    std::string raw =
        "running 10 tests\n"
        "test foo ... ok\n"
        "test bar ... FAILED\n"
        "failures:\n"
        "    bar: assertion failed\n"
        "test result: FAILED. 1 failed; 9 passed\n";
    auto r = f->filter(raw, "cargo test");
    ASSERT_CONTAINS(r.output, "FAILED");
    ASSERT_NOT_CONTAINS(r.output, "test foo ... ok");
}

TEST("test filter: all passing — shows summary") {
    auto* f = getFilter("test");
    std::string raw =
        "running 5 tests\n"
        "test a ... ok\n"
        "test b ... ok\n"
        "test result: ok. 5 passed\n";
    auto r = f->filter(raw, "cargo test");
    ASSERT_CONTAINS(r.output, "test result:");
}

// ---- Search filter ---------------------------------------------------------

TEST("search filter: groups by file") {
    auto* f = getFilter("search");
    std::string raw =
        "src/foo.cpp:10: match one\n"
        "src/foo.cpp:20: match two\n"
        "src/bar.cpp:5: another match\n";
    auto r = f->filter(raw, "grep -r pattern src/");
    ASSERT_CONTAINS(r.output, "src/foo.cpp");
    ASSERT_CONTAINS(r.output, "src/bar.cpp");
}

// Regression: git diff --stat output (file-summary lines, no @@) must pass
// through. Before fix the hunk-anchored filter dropped every line because
// in_hunk never became true.
TEST("git filter: diff --stat passes through file-summary lines") {
    auto f = Reg::instance().create("git");
    ASSERT_TRUE(f != nullptr);
    std::string raw =
        " Core.cs       | 15 +++++++++++++++\n"
        " Program.cs    |  3 ++-\n"
        " Sync/X.cs     | 12 ++++++------\n"
        " 3 files changed, 25 insertions(+), 5 deletions(-)\n";
    auto r = f->filter(raw, "git diff --cached --stat");
    ASSERT_CONTAINS(r.output, "Core.cs");
    ASSERT_CONTAINS(r.output, "Program.cs");
    ASSERT_CONTAINS(r.output, "3 files changed");
}

TEST("git filter: diff --name-only passes through") {
    auto f = Reg::instance().create("git");
    std::string raw = "src/Core.cs\nsrc/Program.cs\n";
    auto r = f->filter(raw, "git diff --name-only HEAD~3");
    ASSERT_CONTAINS(r.output, "src/Core.cs");
    ASSERT_CONTAINS(r.output, "src/Program.cs");
}

// =========================================================================
// Phase 21 Task 5c — DB filter
// =========================================================================

TEST("db filter: mysql tabular output trimmed to header + N rows + footer") {
    auto f = Reg::instance().create("db");
    ASSERT_TRUE(f != nullptr);
    // Synthesize 50-row mysql output
    std::string raw =
        "+----+--------+\n"
        "| id | name   |\n"
        "+----+--------+\n";
    for (int i = 1; i <= 50; ++i) {
        raw += "| " + std::to_string(i) + " | row" + std::to_string(i) + " |\n";
    }
    raw += "+----+--------+\n";
    raw += "50 rows in set (0.01 sec)\n";

    auto r = f->filter(raw, "mysql -e \"SELECT * FROM t\"");
    ASSERT_CONTAINS(r.output, "id");
    ASSERT_CONTAINS(r.output, "row1");
    ASSERT_CONTAINS(r.output, "50 rows in set");
    ASSERT_CONTAINS(r.output, "truncated");
    // 50 raw rows + borders → ~25 lines kept (header + 20 data + truncate marker + footer)
    ASSERT_TRUE(r.filtered_lines < r.original_lines);
}

TEST("db filter: sqlcmd error preserved") {
    auto f = Reg::instance().create("db");
    std::string raw =
        "Msg 208, Level 16, State 1, Server SRV1, Line 1\n"
        "Invalid object name 'foo'.\n";
    auto r = f->filter(raw, "sqlcmd -Q \"SELECT * FROM foo\"");
    ASSERT_CONTAINS(r.output, "Msg 208");
    ASSERT_CONTAINS(r.output, "Invalid object name");
}

TEST("db filter: psql NOTICE/ERROR preserved") {
    auto f = Reg::instance().create("db");
    std::string raw =
        "NOTICE:  CREATE TABLE will create implicit sequence\n"
        "ERROR:  relation \"foo\" already exists\n"
        " id | name\n"
        "----+-------\n"
        "  1 | a\n"
        "(1 row)\n";
    auto r = f->filter(raw, "psql -c \"SELECT ...\"");
    ASSERT_CONTAINS(r.output, "NOTICE");
    ASSERT_CONTAINS(r.output, "ERROR");
    ASSERT_CONTAINS(r.output, "(1 row)");
}

TEST("db filter: dump command falls through unchanged") {
    auto f = Reg::instance().create("db");
    std::string raw = "CREATE TABLE foo (id INT);\nINSERT INTO foo VALUES (1);\n";
    auto r = f->filter(raw, "mysqldump mydb");
    // dump → pass-through (substring match on "dump")
    ASSERT_CONTAINS(r.output, "CREATE TABLE foo");
    ASSERT_CONTAINS(r.output, "INSERT INTO foo");
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
