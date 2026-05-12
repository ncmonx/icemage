// Unit tests for RuleDaemon evaluation logic (threshold + response shape).
// Tests run against the evaluate logic directly (no IPC needed).
#include "../test_main.hpp"
#include "../../src/daemon/rule_daemon.hpp"
#include <filesystem>
#include <fstream>
#include <string>

// Access evaluate() via a thin test subclass (white-box).
class TestDaemon : public icmg::daemon::RuleDaemon {
public:
    explicit TestDaemon() : RuleDaemon(":memory:") {}
    std::string testEval(const std::string& json) {
        // evaluate is private — test via public check path using a temp file
        return ""; // placeholder; see inline tests below
    }
};

// ---- helpers ---------------------------------------------------------------

static std::string makeTempFile(int line_count) {
    std::string path = std::filesystem::temp_directory_path().string()
                     + "/test_daemon_" + std::to_string(line_count) + ".txt";
    std::ofstream f(path);
    for (int i = 0; i < line_count; ++i) f << "line " << i << "\n";
    return path;
}

// ---- tests: countLines -----------------------------------------------------

TEST("rule_daemon: countLines small file") {
    auto path = makeTempFile(10);
    int n = icmg::daemon::RuleDaemon::countLines(path, 1000);
    ASSERT_EQ(n, 10);
    std::filesystem::remove(path);
}

TEST("rule_daemon: countLines stops at max_count") {
    auto path = makeTempFile(600);
    int n = icmg::daemon::RuleDaemon::countLines(path, 100);
    ASSERT_EQ(n, 100);
    std::filesystem::remove(path);
}

TEST("rule_daemon: countLines missing file returns 0") {
    int n = icmg::daemon::RuleDaemon::countLines("/nonexistent/file.txt", 1000);
    ASSERT_EQ(n, 0);
}

// ---- tests: pipeName -------------------------------------------------------

TEST("rule_daemon: pipeName is non-empty") {
    auto name = icmg::daemon::RuleDaemon::pipeName();
    ASSERT_TRUE(!name.empty());
#ifdef _WIN32
    ASSERT_TRUE(name.find("pipe") != std::string::npos);
#else
    ASSERT_TRUE(name.find(".sock") != std::string::npos);
#endif
}

// ---- tests: resolveSuggest -------------------------------------------------

TEST("rule_daemon: resolveSuggest replaces {file}") {
    std::string result = icmg::daemon::RuleDaemon::resolveSuggest(
        "icmg context {file}", "/path/to/file.cpp");
    ASSERT_EQ(result, std::string("icmg context /path/to/file.cpp"));
}

TEST("rule_daemon: resolveSuggest no placeholder passes through") {
    std::string result = icmg::daemon::RuleDaemon::resolveSuggest(
        "icmg context", "/path/to/file.cpp");
    ASSERT_EQ(result, std::string("icmg context"));
}

// ---- tests: threshold logic (via file creation) ----------------------------

TEST("rule_daemon: small file (50 lines) → ALLOW") {
    auto path = makeTempFile(50);
    icmg::daemon::RuleDaemon daemon(":memory:");
    auto result = daemon.checkFile("Read", path);
    ASSERT_EQ(result.action, std::string("ALLOW"));
    std::filesystem::remove(path);
}

TEST("rule_daemon: warn zone (250 lines) → WARN") {
    auto path = makeTempFile(250);
    icmg::daemon::RuleDaemon daemon(":memory:");
    auto result = daemon.checkFile("Read", path);
    ASSERT_EQ(result.action, std::string("WARN"));
    ASSERT_TRUE(!result.message.empty());
    std::filesystem::remove(path);
}

TEST("rule_daemon: block zone (600 lines) → BLOCK") {
    auto path = makeTempFile(600);
    icmg::daemon::RuleDaemon daemon(":memory:");
    auto result = daemon.checkFile("Read", path);
    ASSERT_EQ(result.action, std::string("BLOCK"));
    ASSERT_TRUE(result.message.find("600") != std::string::npos
             || result.message.find("500") != std::string::npos);
    std::filesystem::remove(path);
}

TEST("rule_daemon: unknown tool → ALLOW") {
    icmg::daemon::RuleDaemon daemon(":memory:");
    auto result = daemon.checkFile("Write", "/any/path.cpp");
    ASSERT_EQ(result.action, std::string("ALLOW"));
}

int main() { return icmg::test::run_all(); }
