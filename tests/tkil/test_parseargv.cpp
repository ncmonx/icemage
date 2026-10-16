#include "../test_main.hpp"
#include "../../src/tkil/runner.hpp"
#include "../../src/cli/run_args.hpp"

// ---- parseArgv unit tests --------------------------------------------------

TEST("parseArgv: simple command") {
    auto argv = icmg::tkil::parseArgv("git status");
    ASSERT_EQ(argv.size(), 2u);
    ASSERT_EQ(argv[0], std::string("git"));
    ASSERT_EQ(argv[1], std::string("status"));
}

TEST("parseArgv: single token") {
    auto argv = icmg::tkil::parseArgv("ls");
    ASSERT_EQ(argv.size(), 1u);
    ASSERT_EQ(argv[0], std::string("ls"));
}

TEST("parseArgv: double-quoted argument") {
    auto argv = icmg::tkil::parseArgv("grep \"hello world\" file.txt");
    ASSERT_EQ(argv.size(), 3u);
    ASSERT_EQ(argv[1], std::string("hello world"));
}

TEST("parseArgv: single-quoted argument") {
    auto argv = icmg::tkil::parseArgv("echo 'it works'");
    ASSERT_EQ(argv.size(), 2u);
    ASSERT_EQ(argv[1], std::string("it works"));
}

TEST("parseArgv: backslash is literal on Windows paths") {
    // Windows paths with backslashes MUST NOT be consumed as escape sequences
    auto argv = icmg::tkil::parseArgv("git add D:\\Data\\file.cpp");
    ASSERT_EQ(argv.size(), 3u);
    ASSERT_EQ(argv[2], std::string("D:\\Data\\file.cpp"));
}

TEST("parseArgv: quoted Windows path with spaces") {
    // Paths with spaces must be quoted; backslashes inside quotes are literal
    auto argv = icmg::tkil::parseArgv("git add \"D:\\My Projects\\file.cpp\"");
    ASSERT_EQ(argv.size(), 3u);
    ASSERT_EQ(argv[2], std::string("D:\\My Projects\\file.cpp"));
}

TEST("parseArgv: escaped double-quote only escape") {
    // \\\" → literal " character
    auto argv = icmg::tkil::parseArgv("echo \\\"hello\\\"");
    ASSERT_EQ(argv.size(), 2u);
    ASSERT_EQ(argv[1], std::string("\"hello\""));
}

TEST("parseArgv: multiple spaces collapsed") {
    auto argv = icmg::tkil::parseArgv("a   b   c");
    ASSERT_EQ(argv.size(), 3u);
}

TEST("parseArgv: empty string") {
    auto argv = icmg::tkil::parseArgv("");
    ASSERT_TRUE(argv.empty());
}

TEST("parseArgv: flags preserved") {
    auto argv = icmg::tkil::parseArgv("git log --oneline --graph");
    ASSERT_EQ(argv.size(), 4u);
    ASSERT_EQ(argv[0], std::string("git"));
    ASSERT_EQ(argv[1], std::string("log"));
    ASSERT_EQ(argv[2], std::string("--oneline"));
    ASSERT_EQ(argv[3], std::string("--graph"));
}

// Regression: `icmg run grep -n "EnsureStockRegistered" "d:/My Projects/file.cs"`
// run_cmd.cpp must re-quote args with spaces so parseArgv recovers the path
// as a single token. Before fix: "d:/My Projects/file.cs" → 3 tokens → grep fail.
TEST("parseArgv: quoted Windows path with spaces stays one token") {
    auto argv = icmg::tkil::parseArgv("grep -n \"foo\" \"d:/My Projects/Sync/SyncOrder.cs\"");
    ASSERT_EQ(argv.size(), 4u);
    ASSERT_EQ(argv[0], std::string("grep"));
    ASSERT_EQ(argv[1], std::string("-n"));
    ASSERT_EQ(argv[2], std::string("foo"));
    ASSERT_EQ(argv[3], std::string("d:/My Projects/Sync/SyncOrder.cs"));
}

TEST("parseArgv: backslash Windows path with spaces stays one token") {
    auto argv = icmg::tkil::parseArgv("git add \"D:\\My Projects\\my file.cs\"");
    ASSERT_EQ(argv.size(), 3u);
    ASSERT_EQ(argv[2], std::string("D:\\My Projects\\my file.cs"));
}


// ---- runQuoteArg <-> parseArgv roundtrip (wflog #16755) --------------------
// run_cmd joins argv tokens with runQuoteArg (escapes \ and "), then
// runCommand re-parses the line with parseArgv on the argv-safe path.
// parseArgv MUST invert runQuoteArg exactly, or backslashes double.

TEST("parseArgv: escaped backslash collapses (inverts runQuoteArg)") {
    // "a\\b" (escaped backslash inside quotes) -> token a\b, NOT a\\b
    auto argv = icmg::tkil::parseArgv("echo \"a\\\\b\"");
    ASSERT_EQ(argv.size(), 2u);
    ASSERT_EQ(argv[1], std::string("a\\b"));
}

TEST("parseArgv: trailing backslash survives via escape") {
    // "dir\\" -> token dir\  (escaped \ must not eat the closing quote)
    auto argv = icmg::tkil::parseArgv("echo \"dir\\\\\"");
    ASSERT_EQ(argv.size(), 2u);
    ASSERT_EQ(argv[1], std::string("dir\\"));
}

TEST("roundtrip: runQuoteArg tokens recovered exactly by parseArgv") {
    using icmg::cli::runQuoteArg;
    std::vector<std::string> tokens = {
        "a\\b",                 // single backslash
        "C:\\dir\\file",        // Windows path
        "say \"hi\"",           // embedded quotes
        "a\\\\b",               // real double backslash
        "dir\\",                // trailing backslash
        "has space",            // space forces quoting
        "plain",                // untouched token
    };
    std::string line;
    for (auto& t : tokens) {
        if (!line.empty()) line += ' ';
        line += runQuoteArg(t);
    }
    auto argv = icmg::tkil::parseArgv(line);
    ASSERT_EQ(argv.size(), tokens.size());
    for (size_t i = 0; i < tokens.size(); ++i)
        ASSERT_EQ(argv[i], tokens[i]);
}

// ---- hasShellOperators (shell-vs-argv routing) ----------------------------

TEST("hasShellOperators: unquoted pipe/redirect/and/semicolon/subshell -> true") {
    using icmg::tkil::hasShellOperators;
    ASSERT_TRUE(hasShellOperators("ls src/ | grep x"));
    ASSERT_TRUE(hasShellOperators("a && b"));
    ASSERT_TRUE(hasShellOperators("a || b"));
    ASSERT_TRUE(hasShellOperators("cmd > out.txt"));
    ASSERT_TRUE(hasShellOperators("cat < in.txt"));
    ASSERT_TRUE(hasShellOperators("a ; b"));
    ASSERT_TRUE(hasShellOperators("echo $(date)"));
    ASSERT_TRUE(hasShellOperators("echo `date`"));
}

TEST("hasShellOperators: quoted operators are literal -> false") {
    using icmg::tkil::hasShellOperators;
    ASSERT_FALSE(hasShellOperators("grep 'a|b' file"));
    ASSERT_FALSE(hasShellOperators("echo \"x && y\""));
    ASSERT_FALSE(hasShellOperators("ls -la src"));
    ASSERT_FALSE(hasShellOperators("git commit -m 'fix; done'"));
}
#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
