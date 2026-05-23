// Phase 84 — unit tests for icmg diff-summary parsing logic.
// Inline mirrors of the regex + hunk-range calculation in DiffSummaryCommand.
#include "../test_main.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ── Inline mirrors ────────────────────────────────────────────────────────────

static const std::regex file_re(R"(^diff --git a/(.+?) b/(.+)$)");
static const std::regex hunk_re(R"(^@@\s+-\d+(?:,\d+)?\s+\+(\d+)(?:,(\d+))?\s+@@)");

struct HunkRange { int start; int end; };

static std::vector<std::string> parseFiles(const std::string& diff) {
    std::vector<std::string> files;
    std::istringstream iss(diff);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (std::regex_match(line, m, file_re))
            files.push_back(m[2].str());
    }
    return files;
}

static std::vector<HunkRange> parseHunks(const std::string& diff) {
    std::vector<HunkRange> ranges;
    std::istringstream iss(diff);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (std::regex_search(line, m, hunk_re)) {
            int start = std::stoi(m[1].str());
            int len = m[2].matched ? std::stoi(m[2].str()) : 1;
            if (len == 0) len = 1;
            ranges.push_back({start, start + len - 1});
        }
    }
    return ranges;
}

// ── file_re tests ─────────────────────────────────────────────────────────────

TEST("diff-summary: file_re extracts b/ path as filename") {
    std::string line = "diff --git a/src/foo.cpp b/src/foo.cpp";
    std::smatch m;
    ASSERT_TRUE(std::regex_match(line, m, file_re));
    ASSERT_EQ(m[2].str(), std::string("src/foo.cpp"));
}

TEST("diff-summary: file_re uses b/ path on rename") {
    std::string line = "diff --git a/src/old.cpp b/src/new.cpp";
    std::smatch m;
    ASSERT_TRUE(std::regex_match(line, m, file_re));
    ASSERT_EQ(m[2].str(), std::string("src/new.cpp"));
}

TEST("diff-summary: file_re rejects non-diff lines") {
    std::smatch m;
    std::string s1 = "index abc..def 100644";
    std::string s2 = "+++ b/src/foo.cpp";
    ASSERT_FALSE(std::regex_match(s1, m, file_re));
    ASSERT_FALSE(std::regex_match(s2, m, file_re));
}

TEST("diff-summary: parseFiles returns one entry per diff --git header") {
    std::string diff =
        "diff --git a/src/a.cpp b/src/a.cpp\n"
        "index 000..111 100644\n"
        "diff --git a/src/b.hpp b/src/b.hpp\n"
        "index 111..222 100644\n";
    auto files = parseFiles(diff);
    ASSERT_EQ((int)files.size(), 2);
    ASSERT_EQ(files[0], std::string("src/a.cpp"));
    ASSERT_EQ(files[1], std::string("src/b.hpp"));
}

TEST("diff-summary: parseFiles empty diff yields no files") {
    auto files = parseFiles("");
    ASSERT_EQ((int)files.size(), 0);
}

TEST("diff-summary: parseFiles handles CRLF line endings") {
    std::string diff = "diff --git a/foo.cpp b/foo.cpp\r\n";
    auto files = parseFiles(diff);
    ASSERT_EQ((int)files.size(), 1);
    ASSERT_EQ(files[0], std::string("foo.cpp"));
}

// ── hunk_re tests ─────────────────────────────────────────────────────────────

TEST("diff-summary: hunk_re extracts start+len basic") {
    std::string line = "@@ -10,3 +20,5 @@ void foo()";
    std::smatch m;
    ASSERT_TRUE(std::regex_search(line, m, hunk_re));
    ASSERT_EQ(std::stoi(m[1].str()), 20);  // start
    ASSERT_EQ(std::stoi(m[2].str()), 5);   // len
}

TEST("diff-summary: hunk_re no comma → len defaults to 1") {
    // @@ -5 +10 @@ — no comma means single-line change
    std::string line = "@@ -5 +10 @@ void bar()";
    std::smatch m;
    ASSERT_TRUE(std::regex_search(line, m, hunk_re));
    ASSERT_EQ(std::stoi(m[1].str()), 10);
    ASSERT_FALSE(m[2].matched);
}

TEST("diff-summary: hunk_re len=0 → clamped to 1 in parseHunks") {
    // @@ +5,0 @@ means pure deletion at line 5; range becomes [5,5]
    std::string diff = "@@ -3,2 +5,0 @@ void baz()\n";
    auto ranges = parseHunks(diff);
    ASSERT_EQ((int)ranges.size(), 1);
    ASSERT_EQ(ranges[0].start, 5);
    ASSERT_EQ(ranges[0].end, 5);  // 5 + 1 - 1
}

TEST("diff-summary: parseHunks multi-hunk single file") {
    std::string diff =
        "@@ -1,2 +1,3 @@ // header\n"
        "@@ -20,1 +22,4 @@ void fn()\n";
    auto ranges = parseHunks(diff);
    ASSERT_EQ((int)ranges.size(), 2);
    ASSERT_EQ(ranges[0].start, 1);
    ASSERT_EQ(ranges[0].end, 3);   // 1 + 3 - 1
    ASSERT_EQ(ranges[1].start, 22);
    ASSERT_EQ(ranges[1].end, 25);  // 22 + 4 - 1
}

TEST("diff-summary: parseHunks no-comma hunk → range [start, start]") {
    std::string diff = "@@ -8 +12 @@ int x;\n";
    auto ranges = parseHunks(diff);
    ASSERT_EQ((int)ranges.size(), 1);
    ASSERT_EQ(ranges[0].start, 12);
    ASSERT_EQ(ranges[0].end, 12);
}

// ── limit + truncation logic ──────────────────────────────────────────────────

TEST("diff-summary: limit truncation — file_count > limit sets truncated flag") {
    int limit = 3;
    int file_count = 0;
    bool truncated = false;

    std::vector<std::string> files = {"a.cpp","b.cpp","c.cpp","d.cpp","e.cpp"};
    for (auto& f : files) {
        ++file_count;
        if (file_count > limit) { truncated = true; (void)f; continue; }
    }
    ASSERT_TRUE(truncated);
    ASSERT_EQ(file_count, 5);
}

TEST("diff-summary: limit not exceeded — truncated stays false") {
    int limit = 10;
    int file_count = 0;
    bool truncated = false;
    for (int i = 0; i < 3; ++i) {
        ++file_count;
        if (file_count > limit) { truncated = true; }
    }
    ASSERT_FALSE(truncated);
}

// ── integration parse: full diff snippet ─────────────────────────────────────

TEST("diff-summary: full diff snippet parses 2 files + correct hunk ranges") {
    std::string diff =
        "diff --git a/src/core/db.cpp b/src/core/db.cpp\n"
        "index aaaa..bbbb 100644\n"
        "--- a/src/core/db.cpp\n"
        "+++ b/src/core/db.cpp\n"
        "@@ -45,3 +45,5 @@ void Db::open() {\n"
        "+    // new comment\n"
        "+    init();\n"
        "diff --git a/src/cli/commands/foo_cmd.cpp b/src/cli/commands/foo_cmd.cpp\n"
        "index cccc..dddd 100644\n"
        "@@ -10 +10 @@ int run() {\n";

    auto files  = parseFiles(diff);
    auto ranges = parseHunks(diff);

    ASSERT_EQ((int)files.size(), 2);
    ASSERT_EQ(files[0], std::string("src/core/db.cpp"));
    ASSERT_EQ(files[1], std::string("src/cli/commands/foo_cmd.cpp"));

    ASSERT_EQ((int)ranges.size(), 2);
    ASSERT_EQ(ranges[0].start, 45);
    ASSERT_EQ(ranges[0].end, 49);   // 45 + 5 - 1
    ASSERT_EQ(ranges[1].start, 10);
    ASSERT_EQ(ranges[1].end, 10);   // no comma → len=1
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
