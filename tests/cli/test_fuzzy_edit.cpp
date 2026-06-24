// tests/cli/test_fuzzy_edit.cpp
// TDD for icmg fuzzy-edit (v2.8.1).
// Tests the three matching levels:
//   L1: exact match
//   L2: whitespace-normalized match (leading indent differs)
//   L3: anchor-line match (first non-empty line of old_string)
// Plus: dry-run, no-match report.

#include "../test_main.hpp"
#include "../../src/cli/fuzzy_edit.hpp"

using namespace icmg::cli;

// ---------------------------------------------------------------------------
// L1: exact match
// ---------------------------------------------------------------------------
TEST("fuzzy_edit: L1 exact match applies replacement") {
    std::string file =
        "int foo() {\n"
        "    return 1;\n"
        "}\n";
    std::string old_str = "    return 1;\n";
    std::string new_str = "    return 42;\n";
    auto result = fuzzyEdit(file, old_str, new_str);
    ASSERT_EQ(result.level, 1);
    ASSERT_EQ(result.ok, true);
    std::string expected =
        "int foo() {\n"
        "    return 42;\n"
        "}\n";
    ASSERT_EQ(result.content, expected);
}

// ---------------------------------------------------------------------------
// L2: whitespace-normalized match (agent used 2-space, file has 4-space)
// ---------------------------------------------------------------------------
TEST("fuzzy_edit: L2 ws-normalized match adapts indentation") {
    std::string file =
        "int foo() {\n"
        "    return 1;\n"  // 4-space indent in file
        "}\n";
    std::string old_str =
        "  return 1;\n";   // agent wrote 2-space (wrong)
    std::string new_str =
        "  return 42;\n";  // agent replacement also 2-space
    auto result = fuzzyEdit(file, old_str, new_str);
    ASSERT_EQ(result.level, 2);
    ASSERT_EQ(result.ok, true);
    // replacement uses file's indentation (4-space)
    ASSERT_TRUE(result.content.find("    return 42;") != std::string::npos);
}

// ---------------------------------------------------------------------------
// L3: anchor-line match (first non-empty line of old_string as anchor)
// ---------------------------------------------------------------------------
TEST("fuzzy_edit: L3 anchor-line match on multi-line block") {
    std::string file =
        "void bar() {\n"
        "    int x = 1;\n"
        "    int y = 2;\n"
        "    return x + y;\n"
        "}\n";
    // old_string has correct first line but rest differs slightly
    std::string old_str =
        "    int x = 1;\n"
        "    int y = 2;\n";
    std::string new_str =
        "    int x = 10;\n"
        "    int y = 20;\n";
    auto result = fuzzyEdit(file, old_str, new_str);
    ASSERT_EQ(result.ok, true);
    ASSERT_TRUE(result.level <= 3);
    ASSERT_TRUE(result.content.find("int x = 10;") != std::string::npos);
    ASSERT_TRUE(result.content.find("int y = 20;") != std::string::npos);
}

// ---------------------------------------------------------------------------
// No match: returns ok=false + closest hint
// ---------------------------------------------------------------------------
TEST("fuzzy_edit: no match returns ok=false with hint") {
    std::string file =
        "int foo() {\n"
        "    return 1;\n"
        "}\n";
    std::string old_str = "this does not exist anywhere in the file";
    std::string new_str = "replacement";
    auto result = fuzzyEdit(file, old_str, new_str);
    ASSERT_EQ(result.ok, false);
    ASSERT_TRUE(!result.hint.empty());
}

// ---------------------------------------------------------------------------
// Dry-run: content unchanged, diff reported
// ---------------------------------------------------------------------------
TEST("fuzzy_edit: dry-run does not modify content") {
    std::string file =
        "int foo() {\n"
        "    return 1;\n"
        "}\n";
    std::string old_str = "    return 1;\n";
    std::string new_str = "    return 42;\n";
    auto result = fuzzyEdit(file, old_str, new_str, /*dry_run=*/true);
    ASSERT_EQ(result.ok, true);
    ASSERT_EQ(result.content, file);
    ASSERT_TRUE(!result.diff.empty());
}

// ---------------------------------------------------------------------------
// CRLF tolerance: file has \r\n, old_string has \n
// ---------------------------------------------------------------------------
TEST("fuzzy_edit: CRLF vs LF tolerance") {
    std::string file =
        "int foo() {\r\n"
        "    return 1;\r\n"
        "}\r\n";
    std::string old_str = "    return 1;\n";  // LF only
    std::string new_str = "    return 99;\n";
    auto result = fuzzyEdit(file, old_str, new_str);
    ASSERT_EQ(result.ok, true);
    ASSERT_TRUE(result.content.find("return 99;") != std::string::npos);
}
