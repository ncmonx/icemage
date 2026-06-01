// v1.4.0 Task 1: target-verify command CLI tests.
// Verifies registration, --help, unknown sub, and fail-soft on empty DB.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cli  = icmg::cli;
namespace core = icmg::core;

// ---- TEST 1: command registered in registry --------------------------------

TEST("target_verify_cmd: registered in command registry") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("target-verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("target-verify"));
}

// ---- TEST 2: --help returns 0 ----------------------------------------------

TEST("target_verify_cmd: --help returns 0") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("target-verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
}

// ---- TEST 3: unknown subcommand returns 1 ----------------------------------

TEST("target_verify_cmd: no-args returns non-zero (usage)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("target-verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({});
    // Empty args → usage → return 1
    ASSERT_EQ(rc, 1);
}

// ---- TEST 4: fail-soft on missing DB → rc=0 --------------------------------

TEST("target_verify_cmd: fail-soft on empty DB returns rc=0") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("target-verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    // "deploy" prompt on empty/missing DB should return 0 (no candidates, no crash)
    int rc = cmd->run({"deploy"});
    ASSERT_EQ(rc, 0);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
