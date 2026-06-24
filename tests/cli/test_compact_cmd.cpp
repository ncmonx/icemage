// tests/cli/test_compact_cmd.cpp
// TDD for icmg compact (v2.8.4).

#include "../test_main.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/registry.hpp"

using icmg::core::Registry;
using icmg::cli::BaseCommand;

TEST("compact: command registered in registry") {
    auto cmd = Registry<BaseCommand>::instance().create("compact");
    ASSERT_TRUE(cmd != nullptr);
}

TEST("compact: --help returns 0") {
    auto cmd = Registry<BaseCommand>::instance().create("compact");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
}

TEST("compact: --print-only returns 0 (no DB needed path)") {
    auto cmd = Registry<BaseCommand>::instance().create("compact");
    ASSERT_TRUE(cmd != nullptr);
    // print-only + out to /dev/null equivalent -- just check rc=0
    int rc = cmd->run({"--print-only", "--quiet"});
    ASSERT_EQ(rc, 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
