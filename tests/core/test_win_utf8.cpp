// #285: icmg must carry non-ASCII text to child processes unchanged on Windows.
// Before: argv came in through the ANSI code page and children were spawned
// with CreateProcessA + an ANSI env block, so U+2502 arrived as cp1252 '|'
// (bytes C2 A6 after bash re-encoded it). These tests run inside icmg_test,
// which has NO UTF-8 manifest, so they prove the spawn path itself is wide
// and does not depend on the process code page.
#include "../test_main.hpp"
#include "../../src/core/win_utf8.hpp"
#include "../../src/core/exec_utils.hpp"
#include <string>
#include <fstream>

using namespace icmg::core;

namespace {
// U+2502 BOX DRAWINGS LIGHT VERTICAL (no cp1252 code point) + e-acute (in
// cp1252, so it double-encoded instead of being lost) + U+1F600 (non-BMP).
const std::string kBox    = "\xE2\x94\x82";
const std::string kEAcute = "\xC3\xA9";
const std::string kSmile  = "\xF0\x9F\x98\x80";

#ifdef _WIN32
// Git Bash reads its command line and environment through the wide API and
// re-encodes to UTF-8, so its `od` output shows exactly what Windows handed
// it. Skip (pass) when it is not installed.
std::string gitBash() {
    for (const char* p : {"C:/Program Files/Git/bin/bash.exe", "C:/msys64/usr/bin/bash.exe"}) {
        std::ifstream f(p);
        if (f.good()) return p;
    }
    return {};
}
std::string squash(const std::string& s) {  // "  e2 94 82\n" -> "e294 82"-free hex run
    std::string r;
    for (char c : s) if (std::isxdigit(static_cast<unsigned char>(c))) r += c;
    return r;
}
#endif
}  // namespace

TEST("win_utf8: utf8 -> wide -> utf8 round-trips BMP and non-BMP text") {
    const std::string s = "a" + kBox + kEAcute + kSmile + "z";
    ASSERT_EQ(wideToUtf8(utf8ToWide(s)), s);
    ASSERT_EQ(wideToUtf8(utf8ToWide("")), std::string());
}

TEST("win_utf8: utf8ToWide decodes code points, not bytes") {
    std::wstring w = utf8ToWide(kBox);
    ASSERT_EQ(w.size(), static_cast<size_t>(1));
    ASSERT_EQ(static_cast<unsigned>(w[0]), 0x2502u);
}

#ifdef _WIN32
TEST("win_utf8: wide env block carries the override verbatim and is double-NUL terminated") {
    std::wstring b = wideEnvBlockWith("ICMG_T285_BLK", "x" + kBox);
    ASSERT_TRUE(b.size() >= 2 && b[b.size() - 1] == L'\0' && b[b.size() - 2] == L'\0');
    ASSERT_TRUE(b.find(L"ICMG_T285_BLK=x\x2502") != std::wstring::npos);
}

TEST("win_utf8: wide env block replaces an existing copy of the variable") {
    SetEnvironmentVariableW(L"ICMG_T285_DUP", L"old");
    std::wstring b = wideEnvBlockWith("ICMG_T285_DUP", "new");
    SetEnvironmentVariableW(L"ICMG_T285_DUP", nullptr);
    ASSERT_TRUE(b.find(L"ICMG_T285_DUP=new") != std::wstring::npos);
    ASSERT_TRUE(b.find(L"ICMG_T285_DUP=old") == std::wstring::npos);
}

TEST("win_utf8: commandLineToUtf8Argv splits and decodes a wide command line") {
    auto v = commandLineToUtf8Argv(L"icmg.exe run \"a b\" \x2502");
    ASSERT_EQ(v.size(), static_cast<size_t>(4));
    ASSERT_EQ(v[2], std::string("a b"));
    ASSERT_EQ(v[3], kBox);
}

TEST("#285 safeExec: non-ASCII argv reaches the child unchanged") {
    std::string bash = gitBash();
    if (bash.empty()) return;
    auto r = safeExec({bash, "-c", "printf '%s' \"$1\" | od -An -tx1", "_", kBox + kEAcute});
    ASSERT_EQ(squash(r.out), std::string("e29482c3a9"));
}

TEST("#285 safeExecShell: non-ASCII command text reaches bash unchanged") {
    if (gitBash().empty()) return;
    auto r = safeExecShell("printf '%s' '" + kBox + "' | od -An -tx1");
    ASSERT_EQ(squash(r.out), std::string("e29482"));
}

TEST("#285 safeExecShell: inherited non-ASCII env var reaches the child unchanged") {
    if (gitBash().empty()) return;
    SetEnvironmentVariableW(L"ICMG_T285_ENV", L"\x2502");
    auto r = safeExecShell("printf '%s' \"$ICMG_T285_ENV\" | od -An -tx1");
    SetEnvironmentVariableW(L"ICMG_T285_ENV", nullptr);
    ASSERT_EQ(squash(r.out), std::string("e29482"));
}
#endif

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
