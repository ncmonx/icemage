// test_safe_rollback_cmd — unit tests for `icmg safe-rollback`.
//
// Tests (4):
//   1. Command registered in Registry.
//   2. No args → rc=1.
//   3. Nonexistent file → rc=1.
//   4. --help → rc=0.
//
// Smoke test note: the full git diff/checkout path requires a live git repo
// with a tracked dirty file. That scenario is covered by manual smoke testing
// described in `icmg safe-rollback --help`. Unit tests focus on argument
// validation and registration only.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <string>
#include <vector>

TEST("safe-rollback: registered in Registry") {
    auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
    ASSERT_TRUE(reg.has("safe-rollback"));
}

TEST("safe-rollback: no args returns 1") {
    auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
    auto cmd = reg.create("safe-rollback");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({});
    ASSERT_EQ(rc, 1);
}

TEST("safe-rollback: nonexistent file returns 1") {
    auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
    auto cmd = reg.create("safe-rollback");
    ASSERT_TRUE(cmd != nullptr);
    // Pass a file path that definitely does not exist.
    int rc = cmd->run({"__nonexistent_file_that_does_not_exist_12345.cpp"});
    ASSERT_EQ(rc, 1);
}

TEST("safe-rollback: --help returns 0") {
    auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
    auto cmd = reg.create("safe-rollback");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
