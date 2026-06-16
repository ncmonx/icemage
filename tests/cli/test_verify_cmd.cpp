// Gap #2 regression (2026-06-16): `icmg verify --command` must work on Windows.
// ROOT: workflow_cmd.cpp hardcoded safeExec({"sh","-c",cmd}) — `sh` is not on
// Windows PATH, so every verify invocation failed there. Fix routes through
// core::safeExecShell (cross-platform shell dispatch). These tests lock in:
//   - command registered
//   - --help returns 0
//   - a trivially-true cross-platform command returns the child's exit_code (0)
//   - a failing command returns non-zero (exit_code passthrough)

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cli  = icmg::cli;
namespace core = icmg::core;

// ---- TEST 1: command registered --------------------------------------------

TEST("verify_cmd: registered in command registry") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("verify"));
}

// ---- TEST 2: --help returns 0 ----------------------------------------------

TEST("verify_cmd: --help returns 0") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->run({"--help"}), 0);
}

// ---- TEST 3: cross-platform success command -> exit_code 0 ------------------
// Before the fix this returned non-zero on Windows ("sh" not found). `echo` is
// a builtin on both cmd.exe/pwsh (Windows) and /bin/sh (POSIX).

TEST("verify_cmd: --command echo returns exit_code 0 (cross-platform)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({"--command", "echo icmg-verify-ok"});
    ASSERT_EQ(rc, 0);
}

// ---- TEST 4: failing command -> non-zero exit_code passthrough --------------

TEST("verify_cmd: --command false-ish returns non-zero (exit passthrough)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    // `exit 3` is honored by cmd.exe, pwsh, and /bin/sh alike.
    int rc = cmd->run({"--command", "exit 3"});
    ASSERT_EQ(rc, 3);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
