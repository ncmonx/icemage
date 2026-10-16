// Regression (2026-10-16): `icmg files` crashed with system error 1113 when
// the scanned tree holds a file name Windows cannot map to the ANSI code
// page (e.g. the Cygwin-mangled "C\uF03AUsers...msvc-build.log" in the repo
// root, or any CJK name on a Latin code page). filename().string(),
// extension().string() and relative().generic_string() all convert through
// the ACP and throw; the escape killed the whole command ("icmg: system
// error code 1113"). Fix: route every path -> string conversion through
// core::pathU8() (UTF-8, never throws), same as find_cmd/maintain_cmd.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/path_u8.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using icmg::core::Registry;

static fs::path makeScratch(const char* tag) {
    fs::path d = fs::temp_directory_path() / (std::string("icmg_files_u8_") + tag);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

static void touch(const fs::path& p) { std::ofstream(p) << "x"; }

TEST("pathU8: converts an unmappable name without throwing") {
#if defined(_WIN32)
    fs::path p(L"C\xF03AUsersmsvc-build.log");
#else
    fs::path p("C:Usersmsvc-build.log");
#endif
    bool threw = false;
    std::string s;
    try { s = icmg::core::pathU8(p); } catch (...) { threw = true; }
    ASSERT_TRUE(!threw);
    ASSERT_TRUE(!s.empty());
}

TEST("files_cmd: survives a filename with no ANSI mapping") {
    auto d = makeScratch("scan");
#if defined(_WIN32)
    // U+F03A is what Cygwin writes for ':' -- unmappable in any ANSI code page.
    touch(d / fs::path(L"C\xF03AUsersmsvc-build.log"));
    touch(d / fs::path(L"\x6F22\x5B57.txt"));
#else
    touch(d / "C:Usersmsvc-build.log");
    touch(d / "kanji.txt");
#endif
    touch(d / "plain.txt");

    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("files");
    ASSERT_TRUE(cmd != nullptr);

    std::streambuf* old_out = std::cout.rdbuf();
    std::streambuf* old_err = std::cerr.rdbuf();
    std::ostringstream sink2, esink2;
    std::cout.rdbuf(sink2.rdbuf());
    std::cerr.rdbuf(esink2.rdbuf());
    int rc = -1;
    bool threw = false;
    try { rc = cmd->run({icmg::core::pathU8(d)}); } catch (...) { threw = true; }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);

    ASSERT_TRUE(!threw);
    ASSERT_EQ(rc, 0);
    // The plain file must be listed; the unmappable one must not kill the scan.
    ASSERT_TRUE(sink2.str().find("plain.txt") != std::string::npos);

    std::error_code ec;
    fs::remove_all(d, ec);
}

TEST("files_cmd: --type filter survives unmappable names") {
    auto d = makeScratch("type");
#if defined(_WIN32)
    touch(d / fs::path(L"C\xF03Aweird.log"));
#else
    touch(d / "C:weird.log");
#endif
    touch(d / "keep.cpp");

    auto cmd = Registry<icmg::cli::BaseCommand>::instance().create("files");
    ASSERT_TRUE(cmd != nullptr);

    std::streambuf* old_out = std::cout.rdbuf();
    std::streambuf* old_err = std::cerr.rdbuf();
    std::ostringstream sink, esink;
    std::cout.rdbuf(sink.rdbuf());
    std::cerr.rdbuf(esink.rdbuf());
    int rc = -1;
    bool threw = false;
    try { rc = cmd->run({"--type", "cpp", icmg::core::pathU8(d)}); }
    catch (...) { threw = true; }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);

    ASSERT_TRUE(!threw);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(sink.str().find("keep.cpp") != std::string::npos);

    std::error_code ec;
    fs::remove_all(d, ec);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
