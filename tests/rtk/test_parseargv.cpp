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

TEST("parseArgv: backslash escape") {
    auto argv = icmg::rtk::parseArgv("cmd a\\ b");
    ASSERT_EQ(argv.size(), 2u);
    ASSERT_EQ(argv[1], std::string("a b"));
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
    ASSERT_EQ(argv.size(), 3u);
    ASSERT_EQ(argv[1], std::string("--oneline"));
    ASSERT_EQ(argv[2], std::string("--graph"));
}

int main() {
    std::cout << "=== parseArgv tests ===\n";
    return icmg::test::run_all();
}
