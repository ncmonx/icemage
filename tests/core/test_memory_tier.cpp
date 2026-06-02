// v2.0.0 externals (Tiered Memory): classify a memory node hot/warm/cold by
// recency + frequency + importance. Pure + header-only so it is unit-testable.

#include "../test_main.hpp"
#include "../../src/imem/memory_tier.hpp"

using namespace icmg::imem;

namespace {
const int64_t DAY = 86400;
const int64_t NOW = 1'000'000'000;  // fixed "now" for deterministic ages
}

TEST("memory tier: critical importance is always hot") {
    // old + rare, but importance=3 (critical)
    ASSERT_TRUE(memoryTier(NOW - 200 * DAY, 1, 3, NOW) == MemTier::Hot);
}

TEST("memory tier: recently used is hot") {
    ASSERT_TRUE(memoryTier(NOW - 1 * DAY, 1, 1, NOW) == MemTier::Hot);
}

TEST("memory tier: high frequency is hot") {
    ASSERT_TRUE(memoryTier(NOW - 60 * DAY, 8, 1, NOW) == MemTier::Hot);
}

TEST("memory tier: medium age/freq is warm") {
    auto t = memoryTier(NOW - 10 * DAY, 2, 1, NOW);
    ASSERT_TRUE(t == MemTier::Warm);
}

TEST("memory tier: old + rare is cold") {
    ASSERT_TRUE(memoryTier(NOW - 120 * DAY, 1, 1, NOW) == MemTier::Cold);
}

TEST("memory tier: names") {
    ASSERT_EQ(std::string(memTierName(MemTier::Hot)), std::string("hot"));
    ASSERT_EQ(std::string(memTierName(MemTier::Warm)), std::string("warm"));
    ASSERT_EQ(std::string(memTierName(MemTier::Cold)), std::string("cold"));
    ASSERT_TRUE(memTierFromName("warm") == MemTier::Warm);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
