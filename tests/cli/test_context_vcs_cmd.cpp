// tests/cli/test_context_vcs_cmd.cpp
// TDD for icmg context-commit/branch/merge (v2.8.4).

#include "../test_main.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/registry.hpp"
#include <filesystem>
#include <fstream>

using icmg::core::Registry;
using icmg::cli::BaseCommand;
namespace fs = std::filesystem;

TEST("context_vcs: context-commit registered") {
    auto cmd = Registry<BaseCommand>::instance().create("context-commit");
    ASSERT_TRUE(cmd != nullptr);
}

TEST("context_vcs: context-branch registered") {
    auto cmd = Registry<BaseCommand>::instance().create("context-branch");
    ASSERT_TRUE(cmd != nullptr);
}

TEST("context_vcs: context-merge registered") {
    auto cmd = Registry<BaseCommand>::instance().create("context-merge");
    ASSERT_TRUE(cmd != nullptr);
}

TEST("context_vcs: context-branch --help returns 0") {
    auto cmd = Registry<BaseCommand>::instance().create("context-branch");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
}

TEST("context_vcs: context-commit --help returns 0") {
    auto cmd = Registry<BaseCommand>::instance().create("context-commit");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
}

TEST("context_vcs: context-merge --help returns 0") {
    auto cmd = Registry<BaseCommand>::instance().create("context-merge");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
}

TEST("context_vcs: context-branch no-arg prints current branch") {
    auto cmd = Registry<BaseCommand>::instance().create("context-branch");
    ASSERT_TRUE(cmd != nullptr);
    // no args = print current; should not crash
    int rc = cmd->run({});
    ASSERT_EQ(rc, 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
