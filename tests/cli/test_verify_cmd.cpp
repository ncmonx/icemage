// Gap #2 regression (2026-06-16): `icmg verify --command` must work on Windows.
// ROOT: workflow_cmd.cpp hardcoded safeExec({"sh","-c",cmd}) — `sh` is not on
// Windows PATH, so every verify invocation failed there. Fix routes through
// core::safeExecShell (cross-platform shell dispatch). These tests lock in:
//   - command registered
//   - --help returns 0 (no DB touched — early-exit before Db ctor)
//   - a trivially-true cross-platform command returns the child's exit_code (0)
//   - a failing command returns non-zero (exit_code passthrough)
//
// DB isolation: tests that invoke --command redirect the project DB to a local
// "verify_test.db" file (cleaned up by test_main.hpp's run_all stale-DB sweep)
// and pre-create the verifications table so ctest (cwd=build/) works without a
// real .icmg/ project directory.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/config.hpp"
#include "../../src/core/db.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cli  = icmg::cli;
namespace core = icmg::core;
namespace fs   = std::filesystem;

// RAII guard: redirects Config to a local test DB file, creates the required
// schema, and clears the override on destruction. The file is named with the
// "_test.db" suffix so run_all()'s stale-sweep deletes it before the next run.
struct VerifyDbGuard {
    std::string db_path;
    explicit VerifyDbGuard(const std::string& name = "verify_test.db")
        : db_path(name)
    {
        // Remove any leftover from a previous crashed run.
        std::error_code ec;
        fs::remove(db_path, ec);

        core::Config::instance().setProjectDbOverride(db_path);

        // Bootstrap the schema that VerifyCommand expects (from 0008_workflow.sql).
        core::Db db(db_path);
        db.run(
            "CREATE TABLE IF NOT EXISTS verifications ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  phase       TEXT,"
            "  command     TEXT NOT NULL,"
            "  exit_code   INTEGER NOT NULL,"
            "  output_hash TEXT,"
            "  output_head TEXT,"
            "  duration_ms INTEGER,"
            "  recorded_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
            ")");
    }
    ~VerifyDbGuard() {
        core::Config::instance().clearProjectDbOverride();
    }
};

// ---- TEST 1: command registered --------------------------------------------

TEST("verify_cmd: registered in command registry") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("verify"));
}

// ---- TEST 2: --help returns 0 (no DB touched) ------------------------------
// After the workflow_cmd.cpp fix, --help early-exits BEFORE Db is constructed,
// so this test no longer needs a real .icmg/ directory.

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
    VerifyDbGuard guard("verify_echo_test.db");
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("verify");
    ASSERT_TRUE(static_cast<bool>(cmd));
    int rc = cmd->run({"--command", "echo icmg-verify-ok"});
    ASSERT_EQ(rc, 0);
}

// ---- TEST 4: failing command -> non-zero exit_code passthrough --------------

TEST("verify_cmd: --command false-ish returns non-zero (exit passthrough)") {
    VerifyDbGuard guard("verify_exit3_test.db");
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
