// TDD (2026-06-15): Feature D — `icmg context <f1> <f2> ...` batch read-many.
// Spec: a context call with 2+ file args pulls a bundle per file in one shot.
// The pure helpers split file args from flag values and rebuild a single-file
// arg vector for per-file dispatch. Failing FIRST: context_batch.hpp absent.

#include "../test_main.hpp"
#include "../../src/cli/context_batch.hpp"

#include <string>
#include <vector>

using icmg::cli::collectContextFiles;
using icmg::cli::singleFileArgs;
using icmg::cli::parseChangedFiles;
using icmg::cli::gitListCmdForFlag;

// 1. Multiple bare file args are all collected, in order.
TEST("context-batch: collects multiple file args") {
    std::vector<std::string> args = {"a.hpp", "b.cpp", "c.ts"};
    auto f = collectContextFiles(args);
    ASSERT_EQ((int)f.size(), 3);
    ASSERT_EQ(f[0], std::string("a.hpp"));
    ASSERT_EQ(f[2], std::string("c.ts"));
}

// 2. A value-flag's value (--lines 5-10) is NOT mistaken for a file.
TEST("context-batch: value-flag value is not a file") {
    std::vector<std::string> args = {"--lines", "5-10", "a.hpp", "b.cpp"};
    auto f = collectContextFiles(args);
    ASSERT_EQ((int)f.size(), 2);
    ASSERT_EQ(f[0], std::string("a.hpp"));
    ASSERT_EQ(f[1], std::string("b.cpp"));
}

// 3. The --key=value form needs no skip; still finds the files.
TEST("context-batch: key=value form handled") {
    std::vector<std::string> args = {"--max-bytes=8192", "x.cpp", "y.cpp"};
    auto f = collectContextFiles(args);
    ASSERT_EQ((int)f.size(), 2);
    ASSERT_EQ(f[0], std::string("x.cpp"));
}

// 4. Boolean flags don't swallow the following file.
TEST("context-batch: boolean flag keeps following file") {
    std::vector<std::string> args = {"--no-cache", "a.hpp", "--full", "b.cpp"};
    auto f = collectContextFiles(args);
    ASSERT_EQ((int)f.size(), 2);
    ASSERT_EQ(f[0], std::string("a.hpp"));
    ASSERT_EQ(f[1], std::string("b.cpp"));
}

// 5. singleFileArgs keeps flags + the chosen file, drops the other files.
TEST("context-batch: singleFileArgs keeps flags + one file") {
    std::vector<std::string> args = {"--lines", "5-10", "a.hpp", "--full", "b.cpp"};
    auto sub = singleFileArgs(args, "b.cpp");
    // expect: --lines 5-10 --full b.cpp  (a.hpp dropped)
    bool has_b = false, has_a = false, has_lines = false, has_val = false, has_full = false;
    for (auto& s : sub) {
        if (s == "b.cpp") has_b = true;
        if (s == "a.hpp") has_a = true;
        if (s == "--lines") has_lines = true;
        if (s == "5-10") has_val = true;
        if (s == "--full") has_full = true;
    }
    ASSERT_TRUE(has_b);
    ASSERT_TRUE(!has_a);
    ASSERT_TRUE(has_lines);
    ASSERT_TRUE(has_val);
    ASSERT_TRUE(has_full);
}

// 6. singleFileArgs keeps only the FIRST occurrence of the chosen file.
TEST("context-batch: singleFileArgs dedups the kept file") {
    std::vector<std::string> args = {"a.hpp", "a.hpp"};
    auto sub = singleFileArgs(args, "a.hpp");
    int count = 0;
    for (auto& s : sub) if (s == "a.hpp") ++count;
    ASSERT_EQ(count, 1);
}

// 7. parseChangedFiles: one path per line, in order.
TEST("context-changed: parses one path per line") {
    auto f = parseChangedFiles("src/a.cpp\nsrc/b.hpp\ntests/c.cpp\n");
    ASSERT_EQ((int)f.size(), 3);
    ASSERT_EQ(f[0], std::string("src/a.cpp"));
    ASSERT_EQ(f[1], std::string("src/b.hpp"));
    ASSERT_EQ(f[2], std::string("tests/c.cpp"));
}

// 8. parseChangedFiles: trims trailing CR (Windows git output).
TEST("context-changed: trims CR and trailing space") {
    auto f = parseChangedFiles("src/a.cpp\r\nsrc/b.hpp  \r\n");
    ASSERT_EQ((int)f.size(), 2);
    ASSERT_EQ(f[0], std::string("src/a.cpp"));
    ASSERT_EQ(f[1], std::string("src/b.hpp"));
}

// 9. parseChangedFiles: skips blank lines.
TEST("context-changed: skips blank lines") {
    auto f = parseChangedFiles("\nsrc/a.cpp\n\n\nsrc/b.hpp\n\n");
    ASSERT_EQ((int)f.size(), 2);
    ASSERT_EQ(f[0], std::string("src/a.cpp"));
    ASSERT_EQ(f[1], std::string("src/b.hpp"));
}

// 10. parseChangedFiles: dedups while preserving first-seen order.
TEST("context-changed: dedups preserving order") {
    auto f = parseChangedFiles("src/a.cpp\nsrc/b.hpp\nsrc/a.cpp\n");
    ASSERT_EQ((int)f.size(), 2);
    ASSERT_EQ(f[0], std::string("src/a.cpp"));
    ASSERT_EQ(f[1], std::string("src/b.hpp"));
}

// 11. parseChangedFiles: empty input -> empty list.
TEST("context-changed: empty input yields empty") {
    auto f = parseChangedFiles("");
    ASSERT_EQ((int)f.size(), 0);
}

// 12. gitListCmdForFlag maps --changed to working-tree diff.
TEST("context-changed: gitListCmdForFlag --changed") {
    ASSERT_EQ(parseChangedFiles("").size(), (size_t)0); // keep parseChangedFiles linked
    ASSERT_TRUE(icmg::cli::gitListCmdForFlag("--changed") == std::string("git diff --name-only HEAD"));
}

// 13. gitListCmdForFlag maps --staged to the cached (index) diff.
TEST("context-changed: gitListCmdForFlag --staged") {
    ASSERT_TRUE(icmg::cli::gitListCmdForFlag("--staged") == std::string("git diff --cached --name-only"));
}

// 14. gitListCmdForFlag returns empty for an unknown flag.
TEST("context-changed: gitListCmdForFlag unknown -> empty") {
    ASSERT_TRUE(icmg::cli::gitListCmdForFlag("--bogus").empty());
}
