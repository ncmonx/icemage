// test_covenant_cmd — failing tests for `icmg covenant` command (TDD, written before impl).
// 2026-06-26: covenant store — deterministic cross-session injection of must-hold rules.
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/core/config.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>

using namespace icmg::core;

// Helper: unique temp DB path per test (ensures isolation).
static std::string tmpDb() {
    namespace fs = std::filesystem;
    auto p = fs::temp_directory_path()
           / ("icmg_cov_test_" + std::to_string(std::rand()) + ".db");
    return p.string();
}

// RAII guard: sets + clears project DB override around each test.
struct DbGuard {
    std::string path;
    explicit DbGuard(const std::string& p) : path(p) {
        Config::instance().setProjectDbOverride(p);
    }
    ~DbGuard() {
        Config::instance().clearProjectDbOverride();
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// 1. Registry: "covenant" command is registered.
TEST("covenant_cmd: registered in Registry") {
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("covenant");
    ASSERT_TRUE(cmd != nullptr);
}

// 2. Schema: covenant table exists after migration.
TEST("covenant_cmd: covenant table exists after migration") {
    auto db = Db(":memory:");
    Migrator migrator("__nonexistent_migrations_dir__");
    migrator.runAll(db);
    int count = 0;
    db.query("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='covenant'", {},
        [&](const Row& r){ count = std::stoi(r[0]); });
    ASSERT_EQ(count, 1);
}

// 3. add then inject emits item verbatim.
TEST("covenant_cmd: add then inject emits item verbatim") {
    DbGuard g(tmpDb());
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("covenant");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"add", "no-silent-build", "Never run cmake --build directly. Use build.ps1.", "--priority", "10"});
    ASSERT_EQ(rc, 0);
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject"});
    std::cout.rdbuf(old);
    std::string out = buf.str();
    ASSERT_CONTAINS(out, "no-silent-build");
    ASSERT_CONTAINS(out, "Never run cmake --build directly");
}

// 4. inject with no items emits nothing (no noise).
TEST("covenant_cmd: inject with empty store emits nothing") {
    DbGuard g(tmpDb());
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("covenant");
    ASSERT_TRUE(cmd != nullptr);
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject"});
    std::cout.rdbuf(old);
    ASSERT_EQ(buf.str(), std::string(""));
}

// 5. inject priority ordering: lower priority number appears first.
TEST("covenant_cmd: inject emits lower priority first") {
    DbGuard g(tmpDb());
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("covenant");
    ASSERT_TRUE(cmd != nullptr);
    cmd->run({"add", "second-rule", "body B", "--priority", "200"});
    cmd->run({"add", "first-rule",  "body A", "--priority", "10"});
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject"});
    std::cout.rdbuf(old);
    std::string out = buf.str();
    auto pos_a = out.find("first-rule");
    auto pos_b = out.find("second-rule");
    ASSERT_TRUE(pos_a < pos_b);
}

// 6. --max-items cap emits visible "[+K more" marker.
TEST("covenant_cmd: inject --max-items emits not-shown marker") {
    DbGuard g(tmpDb());
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("covenant");
    ASSERT_TRUE(cmd != nullptr);
    cmd->run({"add", "r1", "body1"});
    cmd->run({"add", "r2", "body2"});
    cmd->run({"add", "r3", "body3"});
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject", "--max-items", "1"});
    std::cout.rdbuf(old);
    ASSERT_CONTAINS(buf.str(), "more");
}

// 7. disable + inject: disabled covenant absent from output.
TEST("covenant_cmd: disabled covenant absent from inject") {
    DbGuard g(tmpDb());
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("covenant");
    ASSERT_TRUE(cmd != nullptr);
    cmd->run({"add", "visible-rule", "keep this"});
    cmd->run({"add", "hidden-rule",  "drop this"});
    cmd->run({"disable", "2"});
    std::ostringstream buf;
    std::streambuf* old = std::cout.rdbuf(buf.rdbuf());
    cmd->run({"inject"});
    std::cout.rdbuf(old);
    std::string out = buf.str();
    ASSERT_CONTAINS(out, "visible-rule");
    ASSERT_NOT_CONTAINS(out, "hidden-rule");
}

// 8. unknown subcmd returns non-zero.
TEST("covenant_cmd: unknown subcmd returns non-zero") {
    DbGuard g(tmpDb());
    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("covenant");
    ASSERT_TRUE(cmd != nullptr);
    ASSERT_EQ(cmd->run({"unknownxyz"}), 1);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

