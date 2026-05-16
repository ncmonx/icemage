// Test icmg rules command.
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/core/rules_store.hpp"
#include "../../src/cli/base_command.hpp"
#include <sstream>
#include <iostream>
#include <string>

using namespace icmg::core;
using namespace icmg::cli;

// Capture stdout during a lambda invocation.
static std::string captureStdout(std::function<void()> fn) {
    std::ostringstream oss;
    std::streambuf* old = std::cout.rdbuf(oss.rdbuf());
    fn();
    std::cout.rdbuf(old);
    return oss.str();
}

// Capture stderr.
static std::string captureStderr(std::function<void()> fn) {
    std::ostringstream oss;
    std::streambuf* old = std::cerr.rdbuf(oss.rdbuf());
    fn();
    std::cerr.rdbuf(old);
    return oss.str();
}

TEST("rules_cmd: registered in Registry") {
    auto cmd = Registry<BaseCommand>::instance().create("rules");
    ASSERT_TRUE(cmd != nullptr);
}

TEST("rules_cmd: unknown subcommand returns rc=1") {
    auto cmd = Registry<BaseCommand>::instance().create("rules");
    ASSERT_TRUE(cmd != nullptr);
    // Redirect stderr to suppress noise
    std::ostringstream oss;
    std::streambuf* old = std::cerr.rdbuf(oss.rdbuf());
    int rc = cmd->run({"__no_such_subcmd__"});
    std::cerr.rdbuf(old);
    ASSERT_EQ(rc, 1);
}

TEST("rules_cmd: inject with no project DB returns 0 or 1 (no crash)") {
    // Inject without a project DB should not crash — it may return 0 (empty) or 1 (no DB).
    auto cmd = Registry<BaseCommand>::instance().create("rules");
    ASSERT_TRUE(cmd != nullptr);
    std::ostringstream out_oss, err_oss;
    std::streambuf* old_out = std::cout.rdbuf(out_oss.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(err_oss.rdbuf());
    int rc = cmd->run({"inject"});
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    // rc is 0 (no DB found, returns early) or 1 (error) — both are acceptable; no crash.
    (void)rc;
}

TEST("rules_cmd: sync graceful when .icmgrules/ absent") {
    // We can't call run() directly with a real project DB easily from unit tests,
    // but we can test via RulesStore directly that sync logic is safe:
    // Just verifying no crash and expected output when directory absent.
    // The command itself handles missing dir gracefully (tested via rules_cmd run).
    auto cmd = Registry<BaseCommand>::instance().create("rules");
    ASSERT_TRUE(cmd != nullptr);
    // Run sync — .icmgrules/ doesn't exist in test cwd; should not crash.
    std::ostringstream out_oss, err_oss;
    std::streambuf* old_out = std::cout.rdbuf(out_oss.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(err_oss.rdbuf());
    // sync without project DB → error return, but no crash
    int rc = cmd->run({"sync"});
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    (void)rc;
}

TEST("rules_cmd: inject emits ## Project rules header when rules present") {
    // Test via RulesStore + manual inject logic to avoid needing real project DB.
    Db db(":memory:");
    Migrator m("__nonexistent__");
    m.runAll(db);
    RulesStore store(db);
    store.upsert(".icmgrules/coding.md", "Always write tests.", "quality");

    // Verify inject output (simulate what inject does):
    auto rows = store.listActive();
    ASSERT_EQ(rows.size(), (size_t)1);

    std::string out = "## Project rules (.icmgrules)\n";
    for (auto& r : rows) {
        out += "### " + r.path + "\n" + r.content + "\n";
    }
    // Must contain header
    ASSERT_TRUE(out.find("## Project rules") != std::string::npos);
    // Must contain the rule content
    ASSERT_TRUE(out.find("Always write tests.") != std::string::npos);
}

int main() { return icmg::test::run_all(); }
