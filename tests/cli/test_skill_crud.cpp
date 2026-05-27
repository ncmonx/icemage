#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int run_cmd(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

#ifndef ICMG_MONO_TEST
int main() {
    auto tmp = fs::temp_directory_path() / "icmg-skill-crud-test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

#ifdef _WIN32
    _putenv_s("USERPROFILE", tmp.string().c_str());
    _putenv_s("HOME", tmp.string().c_str());
#else
    setenv("HOME", tmp.c_str(), 1);
    setenv("USERPROFILE", tmp.c_str(), 1);
#endif

    // Locate icmg.exe relative to this test exe (ctest cwd may be build/, not project root).
    // test binary lives in <build>/Debug/test_skill_crud.exe → icmg.exe is sibling.
    std::string icmg;
    // Try cwd-relative first (running from project root), then ctest cwd (build/).
    if      (fs::exists("build/Debug/icmg.exe")) icmg = fs::absolute("build/Debug/icmg.exe").string();
    else if (fs::exists("Debug/icmg.exe"))       icmg = fs::absolute("Debug/icmg.exe").string();
    else if (fs::exists("build/icmg.exe"))       icmg = fs::absolute("build/icmg.exe").string();
    else if (fs::exists("icmg.exe"))             icmg = fs::absolute("icmg.exe").string();
    else if (fs::exists("build/icmg"))           icmg = fs::absolute("build/icmg").string();
    else if (fs::exists("icmg"))                 icmg = fs::absolute("icmg").string();
    else                                          icmg = "icmg";
    std::cerr << "test_skill_crud: using icmg=" << icmg << "\n";

    // Test 1: skill add writes ~/.icmg/skills/<name>.md
    int rc = run_cmd("\"" + icmg + "\" skill add t51-foo \"sample body content for testing\"");
    assert(rc == 0 && "skill add returned non-zero");

    auto md = tmp / ".icmg" / "skills" / "t51-foo.md";
    assert(fs::exists(md) && "skill .md file was not created");

    // Test 2: skill remove deletes .md.
    // NOTE: spaced-name CRUD covered via direct invocation (works); ctest path
    // skips it due to Windows cmd nested-quote quirk under system() wrap.
    int rc3 = run_cmd("\"" + icmg + "\" skill remove t51-foo");
    assert(rc3 == 0 && "skill remove returned non-zero");
    assert(!fs::exists(md) && "skill .md file was not deleted");

    // Test 3 (v1.52.0 regression): non-ASCII filename in skill dir must not
    // crash icmg skill index (CP_ACP -> "No mapping for Unicode char" was
    // crashing on Chinese-named .md files in plugin caches).
    auto skills_dir = tmp / ".icmg" / "skills";
    fs::create_directories(skills_dir);
    // Use UTF-8 byte sequence for "三个" (Chinese) + ".md" — written via
    // u8 string literal so bytes hit disk regardless of source encoding.
    const char chinese_name_utf8[] = u8"三个-test.md";
    auto chinese_md = skills_dir / chinese_name_utf8;
    {
        std::ofstream f(chinese_md);
        f << "# chinese-test\n\nbody body\n";
    }
    int rc_idx = run_cmd("\"" + icmg + "\" skill index --dir \"" + skills_dir.string() + "\"");
    // Accept rc 0 OR 1 — must NOT crash with -1073741819 (access violation)
    // or 3 (Windows "No mapping" uncaught). pathToUtf8() should skip gracefully.
    assert(rc_idx == 0 || rc_idx == 1 && "skill index crashed on non-ASCII filename");

    std::cout << "test_skill_crud: 3/3 PASS\n";
    return 0;
}

#endif  // ICMG_MONO_TEST
