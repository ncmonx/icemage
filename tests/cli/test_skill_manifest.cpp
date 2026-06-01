// v1.2.0 — contract test for `icmg skill manifest` subcommand.
//
// Asserts:
//   1. Subcommand registered + dispatched (return code 0).
//   2. Unknown subcommand still returns 1 (regression guard).
//
// Note: we don't seed the DB here — populating context_nodes via the public
// API requires standing up Config + Db against a temp project, which is
// covered by integration smoke. The subcommand-dispatch contract is what
// the SessionStart hook depends on.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <memory>
#include <string>

TEST("skill manifest subcommand dispatches (rc=0)") {
    auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({"manifest", "--json"});
    ASSERT_EQ(rc, 0);
}

TEST("skill unknown subcommand returns 1") {
    auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
    auto cmd = reg.create("skill");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({"this-does-not-exist"});
    ASSERT_EQ(rc, 1);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
