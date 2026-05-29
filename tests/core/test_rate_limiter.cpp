// v1.72.0 Security: token-bucket rate limiter for MCP calls.

#include "../test_main.hpp"
#include "../../src/core/rate_limiter.hpp"

using icmg::core::RateLimiter;

TEST("rate-limiter: allows up to capacity as a burst") {
    RateLimiter rl(5.0, 1.0);   // 5 burst, 1/sec refill
    int ok = 0;
    for (int i = 0; i < 5; ++i) if (rl.tryAcquire("t", 100.0)) ok++;
    ASSERT_EQ(ok, 5);
}

TEST("rate-limiter: denies once bucket is empty at same instant") {
    RateLimiter rl(3.0, 1.0);
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(rl.tryAcquire("t", 50.0));
    ASSERT_FALSE(rl.tryAcquire("t", 50.0));   // 4th at same time -> denied
}

TEST("rate-limiter: refills over elapsed time") {
    RateLimiter rl(2.0, 1.0);   // 1 token/sec
    ASSERT_TRUE(rl.tryAcquire("t", 0.0));
    ASSERT_TRUE(rl.tryAcquire("t", 0.0));
    ASSERT_FALSE(rl.tryAcquire("t", 0.0));     // empty
    ASSERT_TRUE(rl.tryAcquire("t", 1.0));      // +1 token after 1s
    ASSERT_FALSE(rl.tryAcquire("t", 1.0));     // empty again
}

TEST("rate-limiter: refill clamps to capacity (no unbounded accrual)") {
    RateLimiter rl(2.0, 1.0);
    // wait a long time, then only `capacity` calls succeed in a burst
    int ok = 0;
    for (int i = 0; i < 10; ++i) if (rl.tryAcquire("t", 1000.0)) ok++;
    ASSERT_EQ(ok, 2);
}

TEST("rate-limiter: buckets are independent per key") {
    RateLimiter rl(1.0, 1.0);
    ASSERT_TRUE(rl.tryAcquire("a", 0.0));
    ASSERT_TRUE(rl.tryAcquire("b", 0.0));   // separate bucket
    ASSERT_FALSE(rl.tryAcquire("a", 0.0));  // a exhausted
}
