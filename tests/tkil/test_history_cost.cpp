// tests/tkil/test_history_cost.cpp
// #1 history-cost -- unit tests for transcript re-send amplification analysis.
#include "../test_main.hpp"
#include "../../src/tkil/history_cost.hpp"

using namespace icmg::tkil;

// Empty history -> zeroed report, no divide-by-zero.
TEST("histcost: empty history is safe") {
    auto rep = analyzeHistoryCost({});
    ASSERT_EQ((int)rep.entries, 0);
    ASSERT_EQ((int)rep.raw_chars, 0);
    ASSERT_EQ((int)rep.amplified_chars, 0);
    ASSERT_TRUE(rep.hotspots.empty());
}

// Amplification math: 3 entries of 100 chars each.
// resends: e0 -> 3, e1 -> 2, e2 -> 1. amplified = 100*(3+2+1) = 600. raw = 300.
TEST("histcost: amplification counts re-sends per position") {
    std::vector<HistEntry> h = {
        {1, 100, "oldest"},
        {2, 100, "middle"},
        {3, 100, "newest"},
    };
    auto rep = analyzeHistoryCost(h);
    ASSERT_EQ((int)rep.entries, 3);
    ASSERT_EQ((int)rep.raw_chars, 300);
    ASSERT_EQ((int)rep.amplified_chars, 600);
    ASSERT_TRUE(rep.amplification == 2.0);
}

// The oldest entry is re-sent the most, so an equal-size oldest entry is the
// top hotspot.
TEST("histcost: oldest equal-size entry is the top hotspot") {
    std::vector<HistEntry> h = {
        {1, 100, "oldest"},
        {2, 100, "middle"},
        {3, 100, "newest"},
    };
    auto rep = analyzeHistoryCost(h);
    ASSERT_EQ((int)rep.hotspots[0].id, 1);
    ASSERT_EQ((int)rep.hotspots[0].resends, 3);
    ASSERT_EQ((int)rep.hotspots[0].amplified, 300);
}

// A big LATE entry can still lose to a moderate EARLY one when amplification
// dominates: e0=50 chars *4 resends=200 vs e3=120*1=120 -> e0 wins.
TEST("histcost: early moderate entry beats big late entry") {
    std::vector<HistEntry> h = {
        {1, 50,  "early"},
        {2, 10,  "x"},
        {3, 10,  "y"},
        {4, 120, "late-big"},
    };
    auto rep = analyzeHistoryCost(h);
    // e0 amplified = 50*4 = 200; e3 amplified = 120*1 = 120.
    ASSERT_EQ((int)rep.hotspots[0].id, 1);
    ASSERT_EQ((int)rep.hotspots[0].amplified, 200);
}

// top-N cap limits hotspot list length.
TEST("histcost: top-N caps hotspot list") {
    std::vector<HistEntry> h;
    for (int i = 0; i < 20; ++i) h.push_back({i + 1, 100, "e"});
    auto rep = analyzeHistoryCost(h, 5);
    ASSERT_EQ((int)rep.hotspots.size(), 5);
    ASSERT_EQ((int)rep.entries, 20);
}

// token estimate ~ chars/4.
TEST("histcost: token estimate is chars over four") {
    ASSERT_EQ((int)histTokens(400), 100);
    ASSERT_EQ((int)histTokens(0), 0);
}
