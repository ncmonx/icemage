// ram-brain Phase A: RecallCache core (LRU + TTL + byte cap + pin) + governor math.
#include "../test_main.hpp"
#include "../../src/core/recall_cache.hpp"
using icmg::core::RecallCache;
using icmg::core::governorTargetBytes;

TEST("recall_cache: put/get hit + miss") {
    RecallCache c; c.setCap(10, 1u<<20);
    c.put("k1", "v1");
    auto v = c.get("k1");
    ASSERT_TRUE(v.has_value());
    ASSERT_EQ(*v, std::string("v1"));
    ASSERT_FALSE(c.get("nope").has_value());
}

TEST("recall_cache: LRU evicts oldest when over entry cap") {
    RecallCache c; c.setCap(2, 1u<<20);
    c.put("a","1"); c.put("b","2");
    (void)c.get("a");           // touch a -> b now LRU
    c.put("c","3");             // over cap -> evict LRU (b)
    ASSERT_TRUE(c.get("a").has_value());
    ASSERT_FALSE(c.get("b").has_value());
    ASSERT_TRUE(c.get("c").has_value());
}

TEST("recall_cache: byte cap evicts") {
    RecallCache c; c.setCap(100, 8);   // 8 bytes incl keys
    c.put("a","12345");
    c.put("b","12345");                // over 8 -> evict a
    ASSERT_FALSE(c.get("a").has_value());
    ASSERT_TRUE(c.get("b").has_value());
}

TEST("recall_cache: TTL expiry (caller clock)") {
    RecallCache c; c.setCap(10, 1u<<20); c.setTtlSeconds(100);
    c.putAt("k","v", 1000);
    ASSERT_TRUE(c.getAt("k", 1099).has_value());
    ASSERT_FALSE(c.getAt("k", 1101).has_value());   // expired
}

TEST("recall_cache: pinned entry survives eviction") {
    RecallCache c; c.setCap(1, 1u<<20);
    c.put("pin","p"); c.pin("pin");
    c.put("other","o");           // over cap, pin protected
    ASSERT_TRUE(c.get("pin").has_value());
    ASSERT_FALSE(c.get("other").has_value());
}

TEST("recall_cache: stats counts hits/misses/entries") {
    RecallCache c; c.setCap(10, 1u<<20);
    c.put("a","1"); (void)c.get("a"); (void)c.get("x");
    auto s = c.stats();
    ASSERT_EQ((int)s.hits, 1);
    ASSERT_EQ((int)s.misses, 1);
    ASSERT_EQ((int)s.entries, 1);
}

TEST("governor: tight RAM shrinks, ample grows, clamped") {
    std::size_t FLOOR = 4u<<20, CEIL = 64u<<20;
    ASSERT_EQ(governorTargetBytes(1000, 32u<<20, FLOOR, CEIL, 10000), (std::size_t)(16u<<20)); // 90% used -> cur/2
    ASSERT_EQ(governorTargetBytes(5000, 16u<<20, FLOOR, CEIL, 10000), (std::size_t)(32u<<20)); // 50% used -> cur*2
    ASSERT_EQ(governorTargetBytes(9000, 60u<<20, FLOOR, CEIL, 10000), CEIL);                   // grow clamp
    ASSERT_EQ(governorTargetBytes(100,  4u<<20,  FLOOR, CEIL, 10000), FLOOR);                  // shrink clamp
    ASSERT_EQ(governorTargetBytes(0,    32u<<20, FLOOR, CEIL, 0),     FLOOR);                  // probe fail
}
