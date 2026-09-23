// TDD (2026-09-23): bench-recall --replay -- IDE-A from the landscape scan
// (docs/plans/2026-09-23-ai-agi-landscape-scan.md; DolphinBench arXiv
// 2609.24971: judge memory by TASK outcome + COST, not just accuracy).
// Replays REAL past recall queries and scores the brain's current answer:
// hit-rate (does it still find what it found?) and token cost (what does an
// answer cost to inject?). Pure aggregation logic -- no DB/IO here.
#include "../test_main.hpp"
#include "../../src/imem/bench_replay.hpp"

using namespace icmg::imem;

static ReplayRow rrow(const std::string& q, int past, int now, int64_t chars) {
    ReplayRow r; r.query = q; r.past_hits = past; r.now_hits = now;
    r.result_chars = chars;
    return r;
}

// 1. Hit = the query still returns at least one result now.
TEST("replay: hit rate counts queries that still resolve") {
    std::vector<ReplayRow> rows = {
        rrow("daemon lifecycle", 3, 2, 400),
        rrow("vulkan hang", 1, 0, 0),        // regressed -> miss
        rrow("release checklist", 2, 5, 900),
    };
    auto s = computeReplayStats(rows);
    ASSERT_EQ(s.total, 3);
    ASSERT_EQ(s.hits, 2);
    ASSERT_TRUE(s.hit_rate > 0.66 && s.hit_rate < 0.67);
}

// 2. Regressions are singled out: had results before, none now.
TEST("replay: regressed queries listed") {
    std::vector<ReplayRow> rows = {
        rrow("a query that worked", 4, 0, 0),
        rrow("still fine", 1, 1, 100),
    };
    auto s = computeReplayStats(rows);
    ASSERT_EQ((int)s.regressed.size(), 1);
    ASSERT_EQ(s.regressed[0], std::string("a query that worked"));
}

// 3. Token cost: ~chars/4, averaged over queries WITH results.
TEST("replay: token estimate averages over answered queries") {
    std::vector<ReplayRow> rows = {
        rrow("q1", 1, 1, 800),    // ~200 tok
        rrow("q2", 1, 2, 1600),   // ~400 tok
        rrow("q3", 1, 0, 0),      // no answer -> excluded from avg
    };
    auto s = computeReplayStats(rows);
    ASSERT_EQ(s.avg_answer_tokens, 300);
    ASSERT_EQ(s.total_answer_tokens, 600);
}

// 4. Empty input -> zeroed stats, no crash, hit_rate 0.
TEST("replay: empty input safe") {
    auto s = computeReplayStats({});
    ASSERT_EQ(s.total, 0);
    ASSERT_EQ(s.hits, 0);
    ASSERT_TRUE(s.hit_rate == 0.0);
    ASSERT_EQ(s.avg_answer_tokens, 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
