#pragma once
// M8 T5: diminishing returns loop guard.
// Stops iterative operations when K consecutive iterations produce < min_delta change.
// Technique from claude-code autoCompact.ts: 3-iteration window, 500-token threshold.

#include <cstdint>

namespace icmg::core {

// Stop iterative work when last `window` ticks all have |delta| < min_delta.
// Usage: call tick(current_value) after each iteration; if returns true, stop.
struct DiminishingReturnsGuard {
    int     window;
    int64_t min_delta;

    explicit DiminishingReturnsGuard(int window_ = 3, int64_t min_delta_ = 500)
        : window(window_), min_delta(min_delta_), low_count_(0), prev_(0) {}

    bool tick(int64_t value) noexcept {
        int64_t delta = value > prev_ ? value - prev_ : prev_ - value;
        prev_ = value;
        if (delta < min_delta) { ++low_count_; } else { low_count_ = 0; }
        return low_count_ >= window;
    }

    void reset() noexcept { low_count_ = 0; prev_ = 0; }

private:
    int     low_count_;
    int64_t prev_;
};

} // namespace icmg::core
