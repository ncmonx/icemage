#include <cassert>
#include <cstdlib>
#include <filesystem>
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

    // Locate installed icmg binary — prefer build output, fall back to PATH.
    std::string icmg;
    if (fs::exists("build/Debug/icmg.exe"))  icmg = "build\\Debug\\icmg.exe";
    else if (fs::exists("build/icmg.exe"))   icmg = "build\\icmg.exe";
    else if (fs::exists("build/icmg"))       icmg = "./build/icmg";
    else                                      icmg = "icmg";

    // Test 1: skill add writes ~/.icmg/skills/<name>.md
    int rc = run_cmd("\"" + icmg + "\" skill add t51-foo \"sample body content for testing\"");
    assert(rc == 0 && "skill add returned non-zero");

    auto md = tmp / ".icmg" / "skills" / "t51-foo.md";
    assert(fs::exists(md) && "skill .md file was not created");

    // Test 2: name with spaces/special chars gets sanitized
    rc = run_cmd("\"" + icmg + "\" skill add \"My Test Skill\" \"body text here\"");
    assert(rc == 0 && "skill add with spaced name returned non-zero");
    auto md2 = tmp / ".icmg" / "skills" / "my-test-skill.md";
    assert(fs::exists(md2) && "sanitized skill .md file was not created");

    // Test 3: skill remove deletes .md.
    int rc3 = run_cmd("\"" + icmg + "\" skill remove t51-foo");
    assert(rc3 == 0 && "skill remove returned non-zero");
    assert(!fs::exists(md) && "skill .md file was not deleted");

    std::cout << "test_skill_crud: 3/3 PASS\n";
    return 0;
}
