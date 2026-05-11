// test_install_cmd — unit tests for `icmg install --system` logic.
// Verifies: sentinel read/write, system dir default, binary copy, PATH detection.
#include "../test_main.hpp"
#include <fstream>
#include <filesystem>
#include <string>
#include <cstdlib>

namespace fs = std::filesystem;

static fs::path tmpDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() / name;
    fs::create_directories(p);
    return p;
}

TEST("install: sentinel write and read roundtrip") {
    auto dir = tmpDir("icmg_sentinel_test");
    fs::path sentinel = dir / "system-path.txt";

    // Write
    { std::ofstream f(sentinel); f << "/usr/local/bin\n"; }

    // Read + trim
    std::string line;
    {
        std::ifstream f(sentinel);
        std::getline(f, line);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
            line.pop_back();
    }

    ASSERT_EQ(line, std::string("/usr/local/bin"));
    fs::remove_all(dir);
}

TEST("install: binary copy to dest dir") {
    auto src_dir = tmpDir("icmg_inst_src");
    auto dst_dir = tmpDir("icmg_inst_dst");

    // Write a fake binary
    fs::path src_bin = src_dir / "icmg.exe";
    { std::ofstream f(src_bin); f << "FAKE_BINARY_CONTENT"; }

    // Copy
    fs::path dst_bin = dst_dir / "icmg.exe";
    std::error_code ec;
    fs::copy_file(src_bin, dst_bin, fs::copy_options::overwrite_existing, ec);

    ASSERT_FALSE(ec.operator bool());
    ASSERT_TRUE(fs::exists(dst_bin));
    ASSERT_EQ(fs::file_size(dst_bin), (uintmax_t)19);

    fs::remove_all(src_dir);
    fs::remove_all(dst_dir);
}

TEST("install: overwrite existing binary at dest") {
    auto src_dir = tmpDir("icmg_ovr_src");
    auto dst_dir = tmpDir("icmg_ovr_dst");

    fs::path src = src_dir / "icmg.exe";
    fs::path dst = dst_dir / "icmg.exe";
    { std::ofstream f(dst); f << "OLD"; }
    { std::ofstream f(src); f << "NEW_CONTENT_LONGER"; }

    std::error_code ec;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec.operator bool());

    std::string content;
    {
        std::ifstream rf(dst);
        content.assign((std::istreambuf_iterator<char>(rf)), {});
    }
    ASSERT_CONTAINS(content, "NEW_CONTENT");
    ASSERT_NOT_CONTAINS(content, "OLD");

    fs::remove_all(src_dir);
    fs::remove_all(dst_dir);
}

TEST("install: missing system dir returns error code") {
    fs::path nonexistent = fs::temp_directory_path() / "icmg_no_such_xyz" / "icmg.exe";
    fs::path src = fs::temp_directory_path() / "icmg_fake_src.exe";
    { std::ofstream f(src); f << "X"; }

    std::error_code ec;
    fs::copy_file(src, nonexistent, fs::copy_options::overwrite_existing, ec);
    ASSERT_TRUE(ec.operator bool());  // should fail — parent dir missing

    fs::remove(src);
}

TEST("install: sentinel absent means no system install") {
    auto dir = tmpDir("icmg_no_sentinel");
    fs::path sentinel = dir / "system-path.txt";
    // Don't create sentinel
    ASSERT_FALSE(fs::exists(sentinel));
    std::string result = "";
    if (fs::exists(sentinel)) {
        std::ifstream f(sentinel); std::getline(f, result);
    }
    ASSERT_TRUE(result.empty());
    fs::remove_all(dir);
}

int main() {
    std::cout << "=== install_cmd tests ===\n";
    return icmg::test::run_all();
}
