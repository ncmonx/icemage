// Issue #277 bug 1: `update --apply` self-test failed, rollback moved the new
// exe aside (.swap) and then could not restore .bak (already archived by the
// self-test doctor), leaving no icmg.exe on PATH while exiting 0.
// rollbackSwap must never end with the target path missing.
#include "../test_main.hpp"
#include "../../src/cli/update_swap.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using icmg::cli::rollbackSwap;
using icmg::cli::touchNow;

namespace {
fs::path freshDir(const char* tag) {
    fs::path d = fs::temp_directory_path() / (std::string("icmg_swap_") + tag);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}
void put(const fs::path& p, const char* s) { std::ofstream(p, std::ios::binary) << s; }
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

TEST("update_swap: rollback restores the backup and removes the swap file") {
    auto d = freshDir("ok");
    auto self = d / "icmg.exe", bak = d / "icmg.exe.bak";
    put(self, "new");
    put(bak, "old");
    auto r = rollbackSwap(self, bak);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(slurp(self), std::string("old"));
    ASSERT_TRUE(!fs::exists(bak));
    ASSERT_TRUE(!fs::exists(d / "icmg.exe.swap"));
}

TEST("update_swap: missing backup keeps the new binary in place") {
    auto d = freshDir("nobak");
    auto self = d / "icmg.exe", bak = d / "icmg.exe.bak";
    put(self, "new");
    auto r = rollbackSwap(self, bak);
    ASSERT_TRUE(!r.ok);
    ASSERT_TRUE(!r.message.empty());
    ASSERT_EQ(slurp(self), std::string("new"));
    ASSERT_TRUE(!fs::exists(d / "icmg.exe.swap"));
}

TEST("update_swap: touchNow resets mtime so doctor does not archive a fresh .bak") {
    auto d = freshDir("touch");
    auto bak = d / "icmg.exe.bak";
    put(bak, "old");
    std::error_code ec;
    fs::last_write_time(bak, fs::file_time_type::clock::now() - std::chrono::hours(24 * 30), ec);
    ASSERT_TRUE(!ec);
    touchNow(bak);
    auto age = fs::file_time_type::clock::now() - fs::last_write_time(bak);
    ASSERT_TRUE(age < std::chrono::hours(1));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
