// tests/cli/test_smart_read_cmd.cpp
// TDD for icmg smart-read (v2.8.3).

#include "../test_main.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/registry.hpp"
#include <fstream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

using icmg::core::Registry;
using icmg::cli::BaseCommand;

static std::string tmpFile(const std::string& name, const std::string& content) {
    auto p = fs::temp_directory_path() / name;
    std::ofstream f(p); f << content;
    return p.string();
}

// ---------------------------------------------------------------------------
TEST("smart_read: command registered in registry") {
    auto cmd = Registry<BaseCommand>::instance().create("smart-read");
    ASSERT_TRUE(cmd != nullptr);
}

TEST("smart_read: --help returns 0") {
    auto cmd = Registry<BaseCommand>::instance().create("smart-read");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
}

TEST("smart_read: nonexistent file returns 1") {
    auto cmd = Registry<BaseCommand>::instance().create("smart-read");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"__nonexistent_file_xyz__.cpp", "--quiet"});
    ASSERT_EQ(rc, 1);
}

TEST("smart_read: small file returns 0") {
    std::string path = tmpFile("sr_small_test.cpp", "int main() { return 0; }\n");
    auto cmd = Registry<BaseCommand>::instance().create("smart-read");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({path, "--quiet"});
    ASSERT_EQ(rc, 0);
    fs::remove(path);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
