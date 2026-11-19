// #285 follow-up: the remaining ANSI Win32 call sites (CreateProcessA,
// GetModuleFileNameA, ShellExecuteA) go through UTF-8 wrappers that call the
// W API. icmg_test has no UTF-8 manifest, so a CJK path survives only if the
// wrapper is really wide: through the ANSI code page it becomes '?'.
#include "../test_main.hpp"
#include "../../src/core/path_u8.hpp"
#include "../../src/core/path_utils.hpp"
#ifdef _WIN32
#  include "../../src/core/win_api_utf8.hpp"
#endif
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace icmg::core;

#ifdef _WIN32
namespace {
// U+2502 (no cp1252 mapping) + U+4E2D (CJK) -> lost by any ANSI hop.
const std::string kDirName = "icmg_t285b_\xE2\x94\x82_\xE4\xB8\xAD";

fs::path freshDir() {
    fs::path d = fs::temp_directory_path() / u8ToPath(kDirName);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

bool runAndWait(const std::string& cmdline, const char* cwd) {
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string buf = cmdline;
    if (!createProcessUtf8(nullptr, buf.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, cwd, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}
}  // namespace

TEST("#285b createProcessUtf8: non-ASCII path in the command line reaches the child") {
    fs::path d = freshDir();
    const std::string target = pathU8(d / "argv_ok.txt");
    ASSERT_TRUE(runAndWait("cmd.exe /d /c type nul > \"" + target + "\"", nullptr));
    ASSERT_TRUE(fs::exists(d / "argv_ok.txt"));
    std::error_code ec; fs::remove_all(d, ec);
}

TEST("#285b createProcessUtf8: non-ASCII working directory is honoured") {
    fs::path d = freshDir();
    const std::string cwd = pathU8(d);
    ASSERT_TRUE(runAndWait("cmd.exe /d /c type nul > cwd_ok.txt", cwd.c_str()));
    ASSERT_TRUE(fs::exists(d / "cwd_ok.txt"));
    std::error_code ec; fs::remove_all(d, ec);
}

TEST("#285b createProcessUtf8: STARTUPINFOA fields carry over (std handles)") {
    fs::path d = freshDir();
    fs::path out = d / "stdout.txt";
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE h = CreateFileW(out.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(h != INVALID_HANDLE_VALUE);
    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h; si.hStdError = h; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::string cl = "cmd.exe /d /c echo hello285";
    ASSERT_TRUE(createProcessUtf8(nullptr, cl.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(h);
    ASSERT_TRUE(fs::file_size(out) >= 9);
    std::error_code ec; fs::remove_all(d, ec);
}

TEST("#285b moduleFileNameUtf8: names an existing file, equals selfExePath") {
    const std::string self = moduleFileNameUtf8();
    ASSERT_FALSE(self.empty());
    ASSERT_TRUE(fs::exists(u8ToPath(self)));
    ASSERT_EQ(selfExePath(), self);
}
#endif

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
