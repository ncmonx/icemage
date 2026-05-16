// test_bug_report_cmd — registry + dispatch contract for `icmg bug-report`.
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

TEST("bug-report cmd registered") {
    auto cmd = icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("bug-report");
    ASSERT_TRUE(cmd != nullptr);
}

TEST("bug-report --help returns 0") {
    auto cmd = icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("bug-report");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
}

TEST("bug-report no-args returns 0 (prints usage)") {
    auto cmd = icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("bug-report");
    int rc = cmd->run({});
    ASSERT_EQ(rc, 0);
}

TEST("bug-report --discard-pending no-pending returns 0") {
    auto cmd = icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("bug-report");
    int rc = cmd->run({"--discard-pending"});
    ASSERT_EQ(rc, 0);
}

TEST("bug-report --list-pending no-pending returns 0") {
    auto cmd = icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("bug-report");
    int rc = cmd->run({"--list-pending"});
    ASSERT_EQ(rc, 0);
}

int main() { return icmg::test::run_all(); }
