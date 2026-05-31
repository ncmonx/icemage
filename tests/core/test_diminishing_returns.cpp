// M8 T5: diminishing returns loop guard.
// Technique from claude-code autoCompact.ts: stop iteration when delta < threshold.
#include "../test_main.hpp"
#include <vector>
#include <cstddef>
#include <cstdint>

// DiminishingReturnsGuard: stops a loop when last K iterations all yield
// delta < min_delta. Prevents wasted work on plateauing operations.
struct DiminishingReturnsGuard {
    int window;       // consecutive low-delta iterations to trigger stop
    int64_t min_delta; // minimum meaningful delta

    explicit DiminishingReturnsGuard(int window_ = 3, int64_t min_delta_ = 500)
        : window(window_), min_delta(min_delta_), low_count_(0), prev_(0) {}

    // Record latest value. Returns true if should stop (diminishing returns).
    bool tick(int64_t value) {
        int64_t delta = value > prev_ ? value - prev_ : prev_ - value;
        prev_ = value;
        if (delta < min_delta) { ++low_count_; } else { low_count_ = 0; }
        return low_count_ >= window;
    }

    void reset() { low_count_ = 0; prev_ = 0; }

private:
    int low_count_;
    int64_t prev_;
};

TEST("diminishing_returns: stops after K low-delta ticks") {
    DiminishingReturnsGuard g(3, 500);
    ASSERT_FALSE(g.tick(1000));  // delta 1000 — big
    ASSERT_FALSE(g.tick(1100));  // delta 100 < 500 (1)
    ASSERT_FALSE(g.tick(1150));  // delta 50  < 500 (2)
    ASSERT_TRUE(g.tick(1160));   // delta 10  < 500 (3) — STOP
}

TEST("diminishing_returns: reset on large delta") {
    DiminishingReturnsGuard g(3, 500);
    g.tick(1000);
    g.tick(1100); // low (1)
    g.tick(1150); // low (2)
    g.tick(2000); // big delta — resets low_count
    ASSERT_FALSE(g.tick(2010)); // low (1) — not stopped yet
}

TEST("diminishing_returns: never stops if deltas stay large") {
    DiminishingReturnsGuard g(3, 500);
    for (int i = 0; i < 100; ++i)
        ASSERT_FALSE(g.tick(i * 1000));
}

TEST("diminishing_returns: window=1 stops immediately on first low") {
    DiminishingReturnsGuard g(1, 500);
    ASSERT_TRUE(g.tick(0));  // delta=0 < 500, count=1 >= window=1 -> stop
}
