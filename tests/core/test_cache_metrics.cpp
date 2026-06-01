// M8 T9: cache metrics normalizer tests.
#include "../test_main.hpp"
#include "../../src/core/cache_metrics.hpp"
#include <vector>

using icmg::core::CacheLayerMetrics;
using icmg::core::aggregateHitRate;
using icmg::core::fmtHitRate;

TEST("cache_metrics: hitRate basic") {
    CacheLayerMetrics m;
    m.hits = 75; m.misses = 25;
    ASSERT_TRUE(m.hitRate() > 0.74 && m.hitRate() < 0.76);
}

TEST("cache_metrics: hitRate null when no requests") {
    CacheLayerMetrics m;
    ASSERT_TRUE(m.hitRate() < 0.0);
}

TEST("cache_metrics: aggregate across layers") {
    std::vector<CacheLayerMetrics> layers(2);
    layers[0].hits = 50; layers[0].misses = 50;
    layers[1].hits = 100; layers[1].misses = 0;
    // total hits 150 / total 200 = 0.75
    double agg = aggregateHitRate(layers);
    ASSERT_TRUE(agg > 0.74 && agg < 0.76);
}

TEST("cache_metrics: unsupported layer excluded from aggregate") {
    std::vector<CacheLayerMetrics> layers(2);
    layers[0].hits = 100; layers[0].misses = 0; layers[0].supported = true;
    layers[1].hits = 0;   layers[1].misses = 100; layers[1].supported = false;
    double agg = aggregateHitRate(layers);
    ASSERT_TRUE(agg > 0.99); // only layer 0 counts -> 100%
}

TEST("cache_metrics: fmtHitRate formats percent") {
    ASSERT_EQ(fmtHitRate(-1.0), std::string("n/a"));
    ASSERT_EQ(fmtHitRate(0.755), std::string("75.5%"));
    ASSERT_EQ(fmtHitRate(1.0), std::string("100.0%"));
}
