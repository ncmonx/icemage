// Tests for `icmg find --name` fuzzy filename locator (find_name.hpp).
// Pure ranking -- no filesystem.

#include "../test_main.hpp"
#include "../../src/cli/find_name.hpp"

#include <string>
#include <vector>

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


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
