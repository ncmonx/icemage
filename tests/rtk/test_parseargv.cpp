#include "../test_main.hpp"
#include "../../src/rtk/runner.hpp"

// ---- parseArgv unit tests --------------------------------------------------

TEST("parseArgv: simple command") {
    auto argv = icmg::rtk::parseArgv("git status");
    ASSERT_EQ(argv.size(), 2u);
    ASSERT_EQ(argv[0], std::string("git"));
    ASSERT_EQ(argv[1], std::string("status"));
}

TEST("parseArgv: single token") {
    auto argv = icmg::rtk::parseArgv("ls");
    ASSERT_EQ(argv.size(), 1u);
    ASSERT_EQ(argv[0], std::string("ls"));
}

TEST("parseArgv: double-quoted argument") {
    auto argv = icmg::rtk::parseArgv("grep \"hello world\" file.txt");
    ASSERT_EQ(argv.size(), 3u);
    ASSERT_EQ(argv[1], std::string("hello world"));
}

TEST("parseArgv: single-quoted argument") {
    auto argv = icmg::rtk::parseArgv("echo 'it works'");
    ASSERT_EQ(argv.size(), 2u);
    ASSERT_EQ(argv[1], std::string("it works"));
}

TEST("parseArgv: backslash is literal on Windows paths") {
    // Windows paths with backslashes MUST NOT be consumed as escape sequences
    auto argv = icmg::rtk::parseArgv("git add D:\\Data\\file.cpp");
    ASSERT_EQ(argv.size(), 3u);
    ASSERT_EQ(argv[2], std::string("D:\\Data\\file.cpp"));
}

TEST("parseArgv: quoted Windows path with spaces") {
    // Paths with spaces must be quoted; backslashes inside quotes are literal
    auto argv = icmg::rtk::parseArgv("git add \"D:\\Data Kerja\\file.cpp\"");
    ASSERT_EQ(argv.size(), 3u);
    ASSERT_EQ(argv[2], std::string("D:\\Data Kerja\\file.cpp"));
}

TEST("parseArgv: escaped double-quote only escape") {
    // \\\" → literal " character
    auto argv = icmg::rtk::parseArgv("echo \\\"hello\\\"");
    ASSERT_EQ(argv.size(), 2u);
    ASSERT_EQ(argv[1], std::string("\"hello\""));
}

TEST("parseArgv: multiple spaces collapsed") {
    auto argv = icmg::rtk::parseArgv("a   b   c");
    ASSERT_EQ(argv.size(), 3u);
}

TEST("parseArgv: empty string") {
    auto argv = icmg::rtk::parseArgv("");
    ASSERT_TRUE(argv.empty());
}

TEST("parseArgv: flags preserved") {
    auto argv = icmg::rtk::parseArgv("git log --oneline --graph");
    ASSERT_EQ(argv.size(), 4u);
    ASSERT_EQ(argv[0], std::string("git"));
    ASSERT_EQ(argv[1], std::string("log"));
    ASSERT_EQ(argv[2], std::string("--oneline"));
    ASSERT_EQ(argv[3], std::string("--graph"));
}

int main() {
    std::cout << "=== parseArgv tests ===\n";
    return icmg::test::run_all();
}
