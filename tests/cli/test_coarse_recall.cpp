// TDD (2026-08-25): brain v2.22 #4 -- coarse-to-fine recall tail collapse.
#include "../test_main.hpp"
#include "../../src/cli/coarse_recall.hpp"
#include <string>

using namespace icmg::cli;
using icmg::imem::MemoryNode;

static MemoryNode nodeWithTok(int64_t id, int approx_tok) {
    MemoryNode n;
    n.id = id;
    // estimateTokens ~ chars/4 -> build content of approx_tok*4 chars.
    n.content = std::string((size_t)approx_tok * 4, 'x');
    return n;
}

TEST("coarse: small result set stays fully expanded") {
    std::vector<MemoryNode> v = {nodeWithTok(1, 100), nodeWithTok(2, 100)};
    ASSERT_EQ(coarseKeepCount(v, 1200, 3), (size_t)2);
}

TEST("coarse: oversized set collapses tail but keeps min_full") {
    std::vector<MemoryNode> v;
    for (int i = 1; i <= 10; ++i) v.push_back(nodeWithTok(i, 500)); // ~5000 tok
    size_t keep = coarseKeepCount(v, 1200, 3);
    ASSERT_EQ(keep, (size_t)3);   // 2 fit in 1200, min_full=3 floors it
}

TEST("coarse: budget extends beyond min_full when it fits") {
    std::vector<MemoryNode> v;
    for (int i = 1; i <= 10; ++i) v.push_back(nodeWithTok(i, 100)); // 1000 total... fits
    ASSERT_EQ(coarseKeepCount(v, 700, 3), (size_t)7);  // 7*100=700 fits, 8th would not
}

TEST("coarse: empty set") {
    std::vector<MemoryNode> v;
    ASSERT_EQ(coarseKeepCount(v, 1200, 3), (size_t)0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
