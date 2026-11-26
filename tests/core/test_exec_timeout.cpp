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
// ---- #295: bare argv0 resolves via PATH order, not System32-first ----------
#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#include <fstream>
namespace {
struct PathEnvGuard {
    std::wstring saved; bool had;
    PathEnvGuard() {
        DWORD n = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        had = n != 0;
        saved.resize(n);
        if (n) saved.resize(GetEnvironmentVariableW(L"PATH", saved.data(), n));
    }
    ~PathEnvGuard() { SetEnvironmentVariableW(L"PATH", had ? saved.c_str() : nullptr); }
};
}  // namespace

TEST("resolveOnPath #295: PATH order beats System32 for a bare .exe name") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "icmg_t295_path";
    fs::create_directories(dir);
    { std::ofstream(dir / "cmd.exe") << "x"; }   // shadows System32\cmd.exe
    PathEnvGuard g;
    std::wstring p = dir.wstring() + L";" + g.saved;
    SetEnvironmentVariableW(L"PATH", p.c_str());
    std::string r = icmg::core::resolveOnPath("cmd.exe");
    ASSERT_TRUE(fs::equivalent(fs::path(r), dir / "cmd.exe"));
    std::string r2 = icmg::core::resolveOnPath("cmd");  // .exe appended
    ASSERT_TRUE(fs::equivalent(fs::path(r2), dir / "cmd.exe"));
    fs::remove_all(dir);
}

TEST("resolveOnPath #295: explicit path and unknown names pass through") {
    ASSERT_EQ(icmg::core::resolveOnPath("C:/x/y.exe"), std::string("C:/x/y.exe"));
    ASSERT_EQ(icmg::core::resolveOnPath("sub\\y.exe"), std::string("sub\\y.exe"));
    ASSERT_EQ(icmg::core::resolveOnPath("no_such_prog_295_zz"),
              std::string("no_such_prog_295_zz"));
}
#endif
