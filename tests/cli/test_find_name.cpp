// Tests for `icmg find --name` fuzzy filename locator (find_name.hpp).
// Pure ranking -- no filesystem.

#include "../test_main.hpp"
#include "../../src/cli/find_name.hpp"

#include <string>
#include <vector>
#include <unordered_map>

using icmg::cli::rankFilenames;
using icmg::cli::scoreFilename;

static std::vector<std::string> sample() {
    return {
        "src/cli/commands/skill_cmd.cpp",
        "src/cli/commands/skill_content.hpp",
        "src/cli/find_name.hpp",
        "src/cli/find_slices.hpp",
        "src/core/hooks/internals.cpp",
        "tests/cli/test_skill_content.cpp",
        "README.md",
    };
}

TEST("find_name: exact basename stem outranks substring matches") {
    auto hits = rankFilenames(sample(), "find_name");
    ASSERT_TRUE(!hits.empty());
    ASSERT_EQ(hits[0].path, std::string("src/cli/find_name.hpp"));
}

TEST("find_name: partial basename matches multiple files") {
    auto hits = rankFilenames(sample(), "skill");
    // skill_cmd.cpp, skill_content.hpp, test_skill_content.cpp all contain 'skill'
    ASSERT_TRUE(hits.size() >= 3);
    for (auto& h : hits) ASSERT_TRUE(h.path.find("skill") != std::string::npos);
}

TEST("find_name: case-insensitive") {
    auto hits = rankFilenames(sample(), "README");
    ASSERT_TRUE(!hits.empty());
    ASSERT_EQ(hits[0].path, std::string("README.md"));
    auto hits2 = rankFilenames(sample(), "readme");
    ASSERT_EQ(hits2[0].path, std::string("README.md"));
}

TEST("find_name: no match returns empty") {
    auto hits = rankFilenames(sample(), "zzznothinghere");
    ASSERT_TRUE(hits.empty());
}

TEST("find_name: prefix beats mid-substring") {
    std::vector<std::string> ps = {"a/xfindy.cpp", "b/find_thing.cpp"};
    auto hits = rankFilenames(ps, "find");
    ASSERT_EQ(hits[0].path, std::string("b/find_thing.cpp"));  // prefix wins
}

TEST("find_name: empty query returns empty") {
    ASSERT_TRUE(rankFilenames(sample(), "").empty());
    ASSERT_TRUE(rankFilenames(sample(), "   ").empty());
}

TEST("find_name: scoreFilename fuzzy subsequence matches scattered chars") {
    // 'scc' is a subsequence of 'skill_content' (s..c..c) -> nonzero
    double s = scoreFilename("src/cli/commands/skill_content.hpp", "sklc");
    ASSERT_TRUE(s > 0.0);
}

TEST("find_name: sortByRecency puts newest mtime first") {
    std::vector<icmg::cli::NameHit> hits = {
        {"old.cpp", 400.0}, {"new.cpp", 100.0}, {"mid.cpp", 200.0}};
    std::unordered_map<std::string, long long> mt = {
        {"old.cpp", 1000}, {"new.cpp", 3000}, {"mid.cpp", 2000}};
    icmg::cli::sortByRecency(hits, mt);
    ASSERT_EQ(hits[0].path, std::string("new.cpp"));
    ASSERT_EQ(hits[1].path, std::string("mid.cpp"));
    ASSERT_EQ(hits[2].path, std::string("old.cpp"));
}

TEST("find_name: sortByRecency tie falls back to name score") {
    std::vector<icmg::cli::NameHit> hits = {
        {"a.cpp", 100.0}, {"b.cpp", 500.0}};
    std::unordered_map<std::string, long long> mt = {
        {"a.cpp", 1000}, {"b.cpp", 1000}};  // same mtime
    icmg::cli::sortByRecency(hits, mt);
    ASSERT_EQ(hits[0].path, std::string("b.cpp"));  // higher score wins tie
}

TEST("find_name: numberLines adds 1-based line numbers") {
    std::string out = icmg::cli::numberLines("alpha\nbeta\ngamma");
    ASSERT_TRUE(out.find("     1  alpha") != std::string::npos);
    ASSERT_TRUE(out.find("     2  beta") != std::string::npos);
    ASSERT_TRUE(out.find("     3  gamma") != std::string::npos);
}

TEST("find_name: numberLines strips trailing CR") {
    std::string out = icmg::cli::numberLines("line1\r\nline2\r\n");
    ASSERT_TRUE(out.find("line1\r") == std::string::npos);
    ASSERT_TRUE(out.find("     1  line1\n") != std::string::npos);
}

TEST("find_name: numberLines caps at maxBytes") {
    std::string big;
    for (int i = 0; i < 5000; ++i) big += "some line content\n";
    std::string out = icmg::cli::numberLines(big, 1000);
    ASSERT_TRUE(out.size() < 1200);
    ASSERT_TRUE(out.find("truncated") != std::string::npos);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
