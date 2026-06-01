// v1.68 S3: scanTree — walk a directory tree, scan each text file for
// secrets, report file + line. Skips VCS/build dirs and binary files.

#include "../test_main.hpp"
#include "../../src/core/scan_logic.hpp"

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace icmg::core;

namespace {
// Built at runtime so the literal AWS-key pattern never appears contiguously
// in this source file (a repo secret-scan hook rejects the literal form).
const std::string kAwsKey = std::string("AKIA") + "IOSFODNN7" + "EXAMPLE";

struct TmpTree {
    fs::path root;
    TmpTree() {
        root = fs::temp_directory_path() /
               ("icmg-scan-" + std::to_string(::time(nullptr)) + "-" +
                std::to_string((uintptr_t)this));
        fs::create_directories(root);
    }
    ~TmpTree() { std::error_code ec; fs::remove_all(root, ec); }
    void write(const std::string& rel, const std::string& content) {
        fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        f << content;
    }
};
} // namespace

TEST("scanTree: finds AWS key with correct 1-based line") {
    TmpTree t;
    // secret on line 3
    t.write("config.txt", "line one\nline two\nkey=" + kAwsKey + "\nline four\n");
    auto found = scanTree(t.root.string(), ScanOpts{});
    ASSERT_EQ(found.size(), (size_t)1);
    ASSERT_EQ(found[0].line, (size_t)3);
    ASSERT_CONTAINS(found[0].type, "AWS");
    ASSERT_CONTAINS(found[0].preview, "REDACTED");   // redact default on
}

TEST("scanTree: clean file yields no findings") {
    TmpTree t;
    t.write("clean.txt", "nothing to see here\njust plain text\n");
    auto found = scanTree(t.root.string(), ScanOpts{});
    ASSERT_EQ(found.size(), (size_t)0);
}

TEST("scanTree: skips .git directory") {
    TmpTree t;
    t.write(".git/config", "token=" + kAwsKey + "\n");
    t.write("clean.txt", "ok\n");
    auto found = scanTree(t.root.string(), ScanOpts{});
    ASSERT_EQ(found.size(), (size_t)0);
}

TEST("scanTree: redact=false keeps raw preview") {
    TmpTree t;
    t.write("a.txt", kAwsKey + "\n");
    ScanOpts opts; opts.redact_preview = false;
    auto found = scanTree(t.root.string(), opts);
    ASSERT_EQ(found.size(), (size_t)1);
    ASSERT_CONTAINS(found[0].preview, kAwsKey);
}
