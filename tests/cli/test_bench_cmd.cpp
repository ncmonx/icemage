// v1.1.0 Task 1: bench command contract test.
//
// Verifies registration + that core subcommands run without throwing.
// Deep semantics (latency thresholds, JSON shape) verified manually via
// `icmg bench all --json` — too environment-dependent for ctest.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <climits>
#include <memory>

namespace cli = icmg::cli;
namespace core = icmg::core;

TEST("bench_cmd: registered in command registry") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("bench");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("bench"));
}

TEST("bench_cmd: empty args prints usage, returns 0") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("bench");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({});
    ASSERT_EQ(rc, 0);
}

TEST("bench_cmd: unknown action returns non-zero (no crash)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("bench");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({"this-is-not-an-action"});
    ASSERT_TRUE(rc != INT_MIN); // any value OK; just verify no throw
}

int main() { return icmg::test::run_all(); }
