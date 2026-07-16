// Regression test (2026-07-10, diagnosed with Cahyo): RuleDaemonClient::
// ensureDaemon() used to spawn the background daemon via safeExecShell()
// with a Windows cmd.exe-specific command string:
//   "start /b icmg rule-daemon start >nul 2>nul"
// safeExecShell() does NOT always route through cmd.exe -- it prefers bash
// whenever bash.exe is found at any of several common paths (MSYS2, Git
// Bash), regardless of whether MSYSTEM/BASH env vars are set. On any Windows
// machine with Git installed (which ships Git Bash), that command was being
// executed by bash instead of cmd.exe. Two things went wrong under bash:
//   1. `start` is a cmd.exe builtin, not a bash command -> "command not
//      found", so the daemon was never actually spawned.
//   2. `nul` is a reserved DEVICE NAME recognized specially by cmd.exe/Win32
//      CreateFile, but bash treats it as an ORDINARY relative filename --
//      so `>nul 2>nul` created a literal file named `nul` in the CURRENT
//      WORKING DIRECTORY. Since the daemon spawn never actually worked,
//      ping() kept failing, so ensureDaemon() re-ran this on every single
//      `icmg` invocation -- creating/touching a stray `nul` file in
//      whatever directory the user happened to run `icmg` from.
//
// Fix: build the spawn as a plain argv array (no shell metacharacters at
// all) and execute it via safeExec() (CreateProcess directly, no shell
// intermediary) instead of safeExecShell(). This is tested here as a pure,
// side-effect-free function so we can assert exactly what would be executed
// without actually spawning a process or touching the filesystem.
#include "../test_main.hpp"
#include "../../src/daemon/rule_daemon_client.hpp"
#include <algorithm>

using icmg::daemon::RuleDaemonClient;

TEST("daemonSpawnArgv: contains no shell redirect metacharacters at all") {
    auto argv = RuleDaemonClient::daemonSpawnArgv();
    for (auto& a : argv) {
        ASSERT_TRUE(a.find('>') == std::string::npos);
        ASSERT_TRUE(a.find('<') == std::string::npos);
        ASSERT_TRUE(a.find('|') == std::string::npos);
        ASSERT_TRUE(a.find('&') == std::string::npos);
    }
}

TEST("daemonSpawnArgv: never contains the literal token \"nul\" (the bug's exact symptom)") {
    auto argv = RuleDaemonClient::daemonSpawnArgv();
    for (auto& a : argv) {
        std::string lower = a;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        ASSERT_TRUE(lower.find("nul") == std::string::npos);
    }
}

TEST("daemonSpawnArgv: the actual daemon command (icmg rule-daemon start) is present verbatim") {
    auto argv = RuleDaemonClient::daemonSpawnArgv();
    bool has_icmg = false, has_rule_daemon = false, has_start = false;
    for (auto& a : argv) {
        if (a == "icmg") has_icmg = true;
        if (a == "rule-daemon") has_rule_daemon = true;
        if (a == "start") has_start = true;
    }
    ASSERT_TRUE(has_icmg);
    ASSERT_TRUE(has_rule_daemon);
    ASSERT_TRUE(has_start);
}

#ifdef _WIN32
TEST("daemonSpawnArgv (Windows): first two tokens are cmd.exe /c (so `start /b` -- a cmd.exe "
     "builtin -- is interpreted by cmd.exe itself, never by whatever shell safeExecShell "
     "might have picked)") {
    auto argv = RuleDaemonClient::daemonSpawnArgv();
    ASSERT_TRUE(argv.size() >= 2);
    ASSERT_EQ(argv[0], std::string("cmd.exe"));
    ASSERT_EQ(argv[1], std::string("/c"));
}
#endif

TEST("daemonSpawnArgv: is deterministic (same input, same output) and non-empty") {
    auto a1 = RuleDaemonClient::daemonSpawnArgv();
    auto a2 = RuleDaemonClient::daemonSpawnArgv();
    ASSERT_TRUE(!a1.empty());
    ASSERT_TRUE(a1 == a2);
}
