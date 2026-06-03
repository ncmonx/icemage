// v2.0.0 C2: cross-turn dedup — word-set Jaccard gate against the injection window.
#include "../test_main.hpp"
#include "../../src/core/cross_turn_dedup.hpp"
#include <string>
#include <vector>
using namespace icmg::core;

TEST("isDuplicateInWindow: exact repeat -> duplicate") {
    std::vector<std::string> win{"the auth token expiry check uses less-than"};
    ASSERT_TRUE(isDuplicateInWindow("the auth token expiry check uses less-than", win, 0.8));
}

TEST("isDuplicateInWindow: unrelated slice -> not duplicate") {
    std::vector<std::string> win{"the auth token expiry check"};
    ASSERT_TRUE(!isDuplicateInWindow("database connection pool sizing", win, 0.8));
}

TEST("isDuplicateInWindow: near-duplicate above threshold -> duplicate") {
    std::vector<std::string> win{"governor budget knapsack pinned survive selection"};
    // same words, one extra -> high Jaccard
    ASSERT_TRUE(isDuplicateInWindow("governor budget knapsack pinned survive selection now", win, 0.7));
}

TEST("isDuplicateInWindow: empty window -> never duplicate") {
    std::vector<std::string> win;
    ASSERT_TRUE(!isDuplicateInWindow("anything here", win, 0.8));
}

TEST("dedupeAgainstWindow: drops window-dups + self-dups, keeps unique") {
    std::vector<std::string> win{"alpha beta gamma delta"};
    std::vector<std::string> slices{
        "alpha beta gamma delta",      // dup of window
        "epsilon zeta eta theta",      // unique
        "epsilon zeta eta theta",      // self-dup of previous
        "lambda mu nu xi"              // unique
    };
    auto kept = dedupeAgainstWindow(slices, win, 0.8);
    ASSERT_EQ(kept.size(), (size_t)2);
    ASSERT_EQ(kept[0], std::string("epsilon zeta eta theta"));
    ASSERT_EQ(kept[1], std::string("lambda mu nu xi"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
