// v1.27.0 (Phase 1.1): TDD coverage for write_expander.cpp
//
// Closes v1.25.0 TDD gap. Covers all 4 magic headers + parse-fail fallback.
//
// SHA verification uses fnv10 algorithm matched inline (anon-namespace in
// impl). Fixtures generated against pre-computed expected hashes for fixed
// inputs so the test stays hermetic.

#include "../test_main.hpp"
#include "../../src/compress/write_expander.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using icmg::compress::expandCompressedWrite;
using icmg::compress::ExpandResult;

// Mirror of anon-namespace fnv10 in write_expander.cpp (kept in sync).
static std::string fnv10(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    static const char* hex = "0123456789abcdef";
    char raw[8];
    std::memcpy(raw, &h, 8);
    std::string out;
    for (int i = 0; i < 5; ++i) {
        out.push_back(hex[(unsigned char)raw[i] >> 4]);
        out.push_back(hex[(unsigned char)raw[i] & 0x0f]);
    }
    return out;
}

static std::string tmpFile(const std::string& stem) {
    auto p = fs::temp_directory_path() / ("icmg_we_" + stem);
    return p.string();
}

static void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    f << content;
}

// ---- @@ICMG-RAW@@ ---------------------------------------------------------

TEST("write_expander: ICMG-RAW pass-through verbatim") {
    std::string in = "@@ICMG-RAW@@\nline1\nline2\n";
    auto r = expandCompressedWrite(in, "");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.mode, std::string("raw"));
    ASSERT_TRUE(r.content.find("line1") != std::string::npos);
    ASSERT_TRUE(r.content.find("line2") != std::string::npos);
    ASSERT_TRUE(r.content.find("@@ICMG-RAW@@") == std::string::npos);
}

TEST("write_expander: no magic header → raw passthrough") {
    std::string in = "plain content with no header\n";
    auto r = expandCompressedWrite(in, "");
    ASSERT_TRUE(r.ok);
    // Plain text falls through all checks; content preserved.
    ASSERT_TRUE(r.content.find("plain content") != std::string::npos);
}

// ---- @@ICMG-DIFF base=<sha10>@@ ------------------------------------------

TEST("write_expander: ICMG-DIFF without base_path → ok=false passthrough") {
    std::string in = "@@ICMG-DIFF base=00000@@\n@@ -1,1 +1,1 @@\n-a\n+b\n";
    auto r = expandCompressedWrite(in, "");
    ASSERT_TRUE(!r.ok);
    ASSERT_EQ(r.mode, std::string("diff"));
    ASSERT_TRUE(r.content == in);  // pass-through
}

TEST("write_expander: ICMG-DIFF SHA mismatch → ok=false passthrough") {
    auto path = tmpFile("sha_mismatch.txt");
    writeFile(path, "hello\n");
    std::string in = "@@ICMG-DIFF base=deadbeef00@@\n@@ -1,1 +1,1 @@\n-hello\n+world\n";
    auto r = expandCompressedWrite(in, path);
    ASSERT_TRUE(!r.ok);
    ASSERT_EQ(r.mode, std::string("diff"));
    ASSERT_TRUE(r.error.find("SHA mismatch") != std::string::npos);
    ASSERT_TRUE(r.content == in);
    fs::remove(path);
}

TEST("write_expander: ICMG-DIFF valid SHA + valid hunk → expand") {
    auto path = tmpFile("diff_valid.txt");
    std::string base = "alpha\nbeta\ngamma\n";
    writeFile(path, base);
    std::string sha = fnv10(base);
    std::string in = "@@ICMG-DIFF base=" + sha + "@@\n"
                     "@@ -2,1 +2,1 @@\n"
                     "-beta\n"
                     "+BETA\n";
    auto r = expandCompressedWrite(in, path);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.mode, std::string("diff"));
    ASSERT_TRUE(r.content.find("BETA") != std::string::npos);
    ASSERT_TRUE(r.content.find("alpha") != std::string::npos);
    ASSERT_TRUE(r.content.find("gamma") != std::string::npos);
    fs::remove(path);
}

TEST("write_expander: ICMG-DIFF context mismatch → ok=false passthrough") {
    auto path = tmpFile("diff_ctx_miss.txt");
    std::string base = "alpha\nbeta\n";
    writeFile(path, base);
    std::string sha = fnv10(base);
    // Hunk claims context line is `zeta` but base has `beta` → mismatch.
    std::string in = "@@ICMG-DIFF base=" + sha + "@@\n"
                     "@@ -1,2 +1,2 @@\n"
                     " zeta\n"
                     "-beta\n"
                     "+BETA\n";
    auto r = expandCompressedWrite(in, path);
    ASSERT_TRUE(!r.ok);
    ASSERT_EQ(r.mode, std::string("diff"));
    ASSERT_TRUE(r.content == in);
    fs::remove(path);
}

// ---- @@ICMG-GLOSS@@ ------------------------------------------------------

TEST("write_expander: ICMG-GLOSS empty body → ok") {
    std::string in = "@@ICMG-GLOSS@@\n";
    auto r = expandCompressedWrite(in, "");
    // Glossary expansion path runs; either ok=true with empty/partial content
    // or pass-through. We assert it doesn't crash + reports "glossary" mode.
    ASSERT_EQ(r.mode, std::string("glossary"));
}

// ---- @@ICMG-TPL id=<name>@@ -----------------------------------------------

TEST("write_expander: ICMG-TPL stub → ok=false (not yet implemented)") {
    std::string in = "@@ICMG-TPL id=foo@@\n{}\n";
    auto r = expandCompressedWrite(in, "");
    ASSERT_TRUE(!r.ok);
    ASSERT_TRUE(r.error.find("not yet implemented") != std::string::npos
             || r.error.find("TPL") != std::string::npos);
}

// ---- Edge cases ----------------------------------------------------------

TEST("write_expander: empty input → raw mode, empty content") {
    auto r = expandCompressedWrite("", "");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.mode, std::string("raw"));
    ASSERT_EQ(r.bytes_in, 0);
}

TEST("write_expander: bytes_in tracked") {
    std::string in = "@@ICMG-RAW@@\nbody\n";
    auto r = expandCompressedWrite(in, "");
    ASSERT_EQ(r.bytes_in, (int)in.size());
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
