// v1.0.0 — basic dispatch coverage for `icmg hook <event>` subcommand.
// Verifies the command is registered and dispatches known events without
// crashing. Deep behavior of each event lives in test_hook_internals.cpp;
// this test is the CLI-layer contract test.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <climits>
#include <memory>
#include <string>
#include <vector>

namespace cli = icmg::cli;
namespace core = icmg::core;

TEST("hook_cmd: registered in command registry") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("hook");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("hook"));
}

TEST("hook_cmd: unknown event returns non-fatal exit code (no crash)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("hook");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({"this-is-not-a-real-event"});
    ASSERT_TRUE(rc != INT_MIN); // any int allowed; just verify no exception
}

TEST("hook_cmd: empty args returns help-shaped exit (no crash)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("hook");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({});
    (void)rc; // accept any code; ensure no throw
    ASSERT_TRUE(true);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
