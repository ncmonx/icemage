// TDD guard for the cache-hit advisor (#1b token-saving backlog):
//   analyze the token-ledger cache-hit rate as a TREND (not just an aggregate),
//   splitting samples chronologically into a prior vs recent half and comparing
//   mean hit-rate. A meaningful DROP means volatile content (timestamps, memory
//   injects) likely leaked into the cached prefix and is busting KV-cache --
//   the advisor flags it so the prefix can be stabilized. Pure -- no DB.
#include "../test_main.hpp"
#include "../../src/cli/cache_advisor.hpp"

using namespace icmg::cli;

// Build a sample whose per-row hit-rate is `hit` (cache_read / total_input),
// with a fixed total so the math is exact.
static CacheSample mk(int64_t ts, double hit) {
    const int64_t total = 1000;
    CacheSample s;
    s.ts = ts;
    s.cache_read = (int64_t)(hit * total);
    s.input = total - s.cache_read;   // fresh
    s.cache_creation = 0;
    return s;
}

TEST("cache-advisor: empty / single -> NoData, empty advice") {
    CacheTrend t = analyzeCacheTrend({});
    ASSERT_TRUE(t.verdict == CacheTrend::NoData);
    ASSERT_TRUE(formatCacheAdvice(t).empty());
    CacheTrend t1 = analyzeCacheTrend({mk(1, 0.9)});
    ASSERT_TRUE(t1.verdict == CacheTrend::NoData);
}

TEST("cache-advisor: per-sample hit-rate math + zero-input guard") {
    ASSERT_TRUE(cacheHitOf(mk(1, 0.8)) > 0.79 && cacheHitOf(mk(1, 0.8)) < 0.81);
    CacheSample z{1, 0, 0, 0};
    ASSERT_TRUE(cacheHitOf(z) == 0.0);
}

TEST("cache-advisor: flat high hit -> Stable, equal halves") {
    std::vector<CacheSample> s;
    for (int i = 0; i < 8; ++i) s.push_back(mk(i, 0.90));
    CacheTrend t = analyzeCacheTrend(s);
    ASSERT_TRUE(t.verdict == CacheTrend::Stable);
    ASSERT_EQ(t.recentN, 4);
    ASSERT_EQ(t.priorN, 4);
    ASSERT_FALSE(formatCacheAdvice(t).empty());
}

TEST("cache-advisor: sharp drop -> Degrading + prefix advice") {
    std::vector<CacheSample> s;
    for (int i = 0; i < 4; ++i) s.push_back(mk(i, 0.90));   // prior: high
    for (int i = 4; i < 8; ++i) s.push_back(mk(i, 0.40));   // recent: low
    CacheTrend t = analyzeCacheTrend(s);
    ASSERT_TRUE(t.verdict == CacheTrend::Degrading);
    ASSERT_TRUE(t.delta < -0.05);
    std::string a = formatCacheAdvice(t);
    ASSERT_FALSE(a.empty());
    ASSERT_TRUE(a.find("prefix") != std::string::npos || a.find("cache") != std::string::npos);
}

TEST("cache-advisor: rising hit -> Improving") {
    std::vector<CacheSample> s;
    for (int i = 0; i < 4; ++i) s.push_back(mk(i, 0.40));
    for (int i = 4; i < 8; ++i) s.push_back(mk(i, 0.90));
    CacheTrend t = analyzeCacheTrend(s);
    ASSERT_TRUE(t.verdict == CacheTrend::Improving);
    ASSERT_TRUE(t.delta > 0.05);
}

TEST("cache-advisor: unsorted input sorted by ts internally") {
    std::vector<CacheSample> s = {mk(7, 0.4), mk(0, 0.9), mk(6, 0.4), mk(1, 0.9),
                                  mk(5, 0.4), mk(2, 0.9), mk(4, 0.4), mk(3, 0.9)};
    CacheTrend t = analyzeCacheTrend(s);
    ASSERT_TRUE(t.verdict == CacheTrend::Degrading);
}
