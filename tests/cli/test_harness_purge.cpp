// Regression (2026-10-15): run_all's stale-DB purge called path.string() on
// every cwd entry. On Windows that converts to the ANSI code page and throws
// system_error 1113 for a name with no mapping -- e.g. a Cygwin-mangled
// "C\uF03AUsers...msvc-build.log" left in the repo root. The throw escaped
// main, the CRT aborted (0xC0000409, bash exit 127, zero output) and
// `icmg verify --command icmg_test.exe` recorded a failure from the repo dir
// while the same binary passed from any other folder.

#include "../test_main.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static fs::path makeScratch(const char* tag) {
    fs::path d = fs::temp_directory_path() / (std::string("icmg_purge_") + tag);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

static void touch(const fs::path& p) { std::ofstream(p) << "x"; }

TEST("test-harness: purgeStaleTestDbs removes *_test.db and its sidecars") {
    auto d = makeScratch("basic");
    touch(d / "a_test.db");
    touch(d / "a_test.db-wal");
    touch(d / "a_test.db-shm");
    touch(d / "keep.db");
    ASSERT_EQ(icmg::test::purgeStaleTestDbs(d), 3);
    ASSERT_TRUE(fs::exists(d / "keep.db"));
    ASSERT_TRUE(!fs::exists(d / "a_test.db"));
    std::error_code ec; fs::remove_all(d, ec);
}

TEST("test-harness: purgeStaleTestDbs survives a filename with no ANSI mapping") {
    auto d = makeScratch("unicode");
#if defined(_WIN32)
    // U+F03A is what Cygwin writes for ':' -- unmappable in any ANSI code page.
    touch(d / fs::path(L"C\xF03AUsersmsvc-build.log"));
    touch(d / fs::path(L"\x6F22\x5B57_test.db"));
#else
    touch(d / "C:Usersmsvc-build.log");
    touch(d / "kanji_test.db");
#endif
    touch(d / "b_test.db");
    int removed = -1;
    bool threw = false;
    try { removed = icmg::test::purgeStaleTestDbs(d); } catch (...) { threw = true; }
    ASSERT_TRUE(!threw);
    ASSERT_EQ(removed, 2);
    std::error_code ec; fs::remove_all(d, ec);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
