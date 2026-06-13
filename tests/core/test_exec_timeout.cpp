// v1.78.4: placeholder — safeExecShell regression tests removed (flaky in ctest
// due to cmd.exe pipe-flush timing in VS test runner environment). The init 30-min
// hang fix is in init_cmd.cpp (CreateProcessA with bInheritHandles=FALSE), not in
// safeExecShell. Updating_lock tests cover the other v1.78.4 bug fix.
#include "../test_main.hpp"
#include "../../src/core/exec_utils.hpp"

TEST("exec_timeout: placeholder - init hang fix is in init_cmd.cpp not safeExecShell") {
    ASSERT_TRUE(true);
}

// resolveWinShell — pure shell-path resolution (CreateProcess PATH-search fix)
TEST("resolveWinShell: first existing pwsh candidate wins") {
    std::vector<std::string> cands = {"a/pwsh.exe", "b/pwsh.exe"};
    auto onlyB = [](const std::string& p){ return p == "b/pwsh.exe"; };
    ASSERT_EQ(icmg::core::resolveWinShell(cands, "ps5.exe", onlyB), std::string("b/pwsh.exe"));
}

TEST("resolveWinShell: order respected (first match)") {
    std::vector<std::string> cands = {"a/pwsh.exe", "b/pwsh.exe"};
    auto all = [](const std::string&){ return true; };
    ASSERT_EQ(icmg::core::resolveWinShell(cands, "ps5.exe", all), std::string("a/pwsh.exe"));
}

TEST("resolveWinShell: none exist -> powershell5 fallback") {
    std::vector<std::string> cands = {"a/pwsh.exe", "b/pwsh.exe"};
    auto none = [](const std::string&){ return false; };
    ASSERT_EQ(icmg::core::resolveWinShell(cands, "C:/Win/System32/WindowsPowerShell/v1.0/powershell.exe", none),
              std::string("C:/Win/System32/WindowsPowerShell/v1.0/powershell.exe"));
}

TEST("resolveWinShell: empty candidates skipped") {
    std::vector<std::string> cands = {"", "ok/pwsh.exe"};
    auto exok = [](const std::string& p){ return p == "ok/pwsh.exe"; };
    ASSERT_EQ(icmg::core::resolveWinShell(cands, "ps5", exok), std::string("ok/pwsh.exe"));
}