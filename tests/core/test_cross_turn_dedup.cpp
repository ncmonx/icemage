// v2.0.0 C2: cross-turn dedup — word-set Jaccard gate against the injection window.
#include "../test_main.hpp"
#include "../../src/core/cross_turn_dedup.hpp"
#include <string>
#include <vector>
#include <cstdio>
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

// v2.0.0 C2 cross-turn wiring: file-backed window persists across turns/process
// runs. First emit of a slice is kept (recorded); a near-dup on a later turn skips.
TEST("dedupAgainstWindowFile: first emit kept, cross-turn near-dup skipped") {
    const char* wf = "test_c2_window.txt";
    std::remove(wf);
    // Turn 1: window empty -> not a dup, recorded.
    ASSERT_TRUE(!dedupAgainstWindowFile("decisions-build linker fix lld auto detect", wf, 0.8));
    // Turn 2 (fresh window load from file): near-identical topic (same words + one
    // extra, e.g. truncation differs), different node id -> Jaccard 7/8=0.875 >= 0.8.
    ASSERT_TRUE(dedupAgainstWindowFile("decisions-build linker fix lld auto detect now", wf, 0.8));
    // Turn 2b: genuinely distinct topic still passes (and is recorded).
    ASSERT_TRUE(!dedupAgainstWindowFile("memory recall bm25 recency blend tuning", wf, 0.8));
    std::remove(wf);
}

TEST("dedupAgainstWindowFile: missing window file -> never a dup, then recorded") {
    const char* wf = "test_c2_window_missing.txt";
    std::remove(wf);
    ASSERT_TRUE(!dedupAgainstWindowFile("alpha beta gamma delta epsilon", wf, 0.8));
    ASSERT_TRUE(dedupAgainstWindowFile("alpha beta gamma delta epsilon", wf, 0.8));
    std::remove(wf);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
