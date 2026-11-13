// Issue #277 (v2.29.0): `init --install-hooks` truncated .claude/settings.local.json
// to 0 bytes (std::ofstream opened before json::dump threw on a non-UTF-8 byte)
// and wrote cp1252 bytes into hook scripts. These helpers make the write
// validate-first and atomic: the target is either the old file or the new one.
#include "../test_main.hpp"
#include "../../src/core/atomic_file.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using icmg::core::isValidUtf8;
using icmg::core::writeFileAtomic;

namespace {
fs::path freshDir(const char* tag) {
    fs::path d = fs::temp_directory_path() / (std::string("icmg_atomic_") + tag);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

TEST("atomic_file: isValidUtf8 accepts ASCII and multi-byte UTF-8") {
    ASSERT_TRUE(isValidUtf8(""));
    ASSERT_TRUE(isValidUtf8("plain ascii"));
    ASSERT_TRUE(isValidUtf8("em \xE2\x80\x94 arrow \xE2\x86\x92 check \xE2\x9C\x93"));
    ASSERT_TRUE(isValidUtf8("emoji \xF0\x9F\x98\x80"));
}

TEST("atomic_file: isValidUtf8 rejects cp1252 bytes and malformed sequences") {
    ASSERT_TRUE(!isValidUtf8("cp1252 em dash \x97"));          // the #277 byte
    ASSERT_TRUE(!isValidUtf8("truncated \xE2\x80"));
    ASSERT_TRUE(!isValidUtf8("overlong \xC0\xAF"));
    ASSERT_TRUE(!isValidUtf8("surrogate \xED\xA0\x80"));
    ASSERT_TRUE(!isValidUtf8("too big \xF4\x90\x80\x80"));
}

TEST("atomic_file: writeFileAtomic replaces content and leaves no temp file") {
    auto d = freshDir("replace");
    auto p = d / "settings.local.json";
    { std::ofstream(p, std::ios::binary) << "old"; }
    std::string err;
    ASSERT_TRUE(writeFileAtomic(p, "{\"new\":true}\n", &err));
    ASSERT_EQ(slurp(p), std::string("{\"new\":true}\n"));
    int files = 0;
    for (auto& e : fs::directory_iterator(d)) { (void)e; ++files; }
    ASSERT_EQ(files, 1);
}

TEST("atomic_file: writeFileAtomic creates a missing file") {
    auto d = freshDir("create");
    auto p = d / "hook.sh";
    ASSERT_TRUE(writeFileAtomic(p, "#!/bin/bash\n", nullptr));
    ASSERT_EQ(slurp(p), std::string("#!/bin/bash\n"));
}

TEST("atomic_file: writeFileAtomic fails cleanly when the directory is missing") {
    auto d = freshDir("nodir");
    auto p = d / "missing" / "x.json";
    std::string err;
    ASSERT_TRUE(!writeFileAtomic(p, "x", &err));
    ASSERT_TRUE(!err.empty());
    ASSERT_TRUE(!fs::exists(p));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
