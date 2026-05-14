// Unit tests for RuleDaemon evaluation logic (threshold + response shape).
// Tests run against the evaluate logic directly (no IPC needed).
#include "../test_main.hpp"
#include "../../src/daemon/rule_daemon.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

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

// ---- tests: B2 dispatcher + concurrency (v0.55.0) --------------------------

TEST("rule_daemon: dispatch PING returns PONG") {
    icmg::daemon::RuleDaemon daemon(":memory:");
    auto res = daemon.dispatch("{\"tool\":\"PING\"}");
    ASSERT_TRUE(res.find("PONG") != std::string::npos);
}

TEST("rule_daemon: dispatch RELOAD returns RELOADED") {
    icmg::daemon::RuleDaemon daemon(":memory:");
    auto res = daemon.dispatch("{\"tool\":\"RELOAD\"}");
    ASSERT_TRUE(res.find("RELOADED") != std::string::npos);
}

TEST("rule_daemon: dispatch SHUTDOWN returns SHUTDOWN") {
    icmg::daemon::RuleDaemon daemon(":memory:");
    auto res = daemon.dispatch("{\"tool\":\"SHUTDOWN\"}");
    ASSERT_TRUE(res.find("SHUTDOWN") != std::string::npos);
}

TEST("rule_daemon: SET_STRICT then GET_STRICT roundtrip") {
    icmg::daemon::RuleDaemon daemon(":memory:");
    daemon.dispatch("{\"tool\":\"SET_STRICT\",\"on\":true}");
    auto res = daemon.dispatch("{\"tool\":\"GET_STRICT\"}");
    ASSERT_TRUE(res.find("\"strict\":true") != std::string::npos);

    daemon.dispatch("{\"tool\":\"SET_STRICT\",\"on\":false}");
    auto res2 = daemon.dispatch("{\"tool\":\"GET_STRICT\"}");
    ASSERT_TRUE(res2.find("\"strict\":false") != std::string::npos);
}

TEST("rule_daemon: dispatch unknown tool falls back to ALLOW") {
    icmg::daemon::RuleDaemon daemon(":memory:");
    auto res = daemon.dispatch("{\"tool\":\"UnknownToolXYZ\"}");
    ASSERT_TRUE(res.find("ALLOW") != std::string::npos);
}

TEST("rule_daemon: dispatch malformed JSON falls back to ALLOW") {
    icmg::daemon::RuleDaemon daemon(":memory:");
    auto res = daemon.dispatch("{not-json");
    ASSERT_TRUE(res.find("ALLOW") != std::string::npos);
}

TEST("rule_daemon: concurrent SET_STRICT from N threads — no data race + valid final state") {
    icmg::daemon::RuleDaemon daemon(":memory:");
    constexpr int N = 16;
    constexpr int iters = 200;
    std::vector<std::thread> workers;
    for (int t = 0; t < N; ++t) {
        workers.emplace_back([&daemon, t]() {
            for (int i = 0; i < iters; ++i) {
                bool on = ((t + i) % 2) == 0;
                std::string req = std::string("{\"tool\":\"SET_STRICT\",\"on\":")
                                + (on ? "true" : "false") + "}";
                daemon.dispatch(req);
                daemon.dispatch("{\"tool\":\"GET_STRICT\"}");
            }
        });
    }
    for (auto& w : workers) w.join();
    // Final deterministic state.
    daemon.dispatch("{\"tool\":\"SET_STRICT\",\"on\":true}");
    auto res = daemon.dispatch("{\"tool\":\"GET_STRICT\"}");
    ASSERT_TRUE(res.find("\"strict\":true") != std::string::npos);
}

TEST("rule_daemon: concurrent checkFile + SET_STRICT — no crash, total accounted") {
    auto path = makeTempFile(600); // BLOCK zone
    icmg::daemon::RuleDaemon daemon(":memory:");
    std::atomic<int> blocks{0}, allows{0}, others{0};

    std::thread toggler([&daemon]() {
        for (int i = 0; i < 500; ++i) {
            bool on = (i % 2) == 0;
            std::string req = std::string("{\"tool\":\"SET_STRICT\",\"on\":")
                            + (on ? "true" : "false") + "}";
            daemon.dispatch(req);
        }
    });

    std::vector<std::thread> readers;
    for (int t = 0; t < 8; ++t) {
        readers.emplace_back([&daemon, &path, &blocks, &allows, &others]() {
            for (int i = 0; i < 200; ++i) {
                auto r = daemon.checkFile("Read", path);
                if      (r.action == "BLOCK") ++blocks;
                else if (r.action == "ALLOW") ++allows;
                else                          ++others;
            }
        });
    }
    toggler.join();
    for (auto& r : readers) r.join();

    int total = blocks.load() + allows.load() + others.load();
    ASSERT_EQ(total, 8 * 200);
    std::filesystem::remove(path);
}

int main() { return icmg::test::run_all(); }
