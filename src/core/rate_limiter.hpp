#pragma once
// v1.72.0 Security: token-bucket rate limiter for MCP tool calls.
//
// A malicious or runaway MCP client can hammer the stdio server with calls.
// Each tool name gets a token bucket (capacity = burst, refill = sustained
// rate). The core logic is pure (caller supplies the timestamp) so it is
// unit-testable without a clock; a process-global instance is used at runtime.
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>

namespace icmg::core {

class RateLimiter {
public:
    // capacity = max burst tokens; refill_per_sec = sustained tokens/sec.
    RateLimiter(double capacity, double refill_per_sec)
        : cap_(capacity), refill_(refill_per_sec) {}

    // Pure, testable: try to consume one token for `key` at absolute time
    // `now_sec`. Returns true if allowed, false if the bucket is empty.
    bool tryAcquire(const std::string& key, double now_sec) {
        std::lock_guard<std::mutex> lk(mu_);
        auto& b = buckets_[key];
        if (!b.init) { b.tokens = cap_; b.last = now_sec; b.init = true; }
        // refill based on elapsed time, clamp to capacity
        double elapsed = now_sec - b.last;
        if (elapsed > 0) {
            b.tokens += elapsed * refill_;
            if (b.tokens > cap_) b.tokens = cap_;
            b.last = now_sec;
        }
        if (b.tokens >= 1.0) { b.tokens -= 1.0; return true; }
        return false;
    }

    double capacity() const { return cap_; }

private:
    struct Bucket { double tokens = 0.0; double last = 0.0; bool init = false; };
    double cap_, refill_;
    std::mutex mu_;
    std::unordered_map<std::string, Bucket> buckets_;
};

// Monotonic-ish wall clock in seconds (for the runtime limiter).
inline double nowSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
        steady_clock::now().time_since_epoch()).count();
}

// Process-global MCP limiter. Configurable via ICMG_MCP_RATE_LIMIT (calls per
// minute, default 240; set to 0 to disable). Burst capacity = one minute's
// worth. Returns true if the call is allowed.
inline bool mcpRateOk(const std::string& tool) {
    static double per_min = [] {
        const char* e = std::getenv("ICMG_MCP_RATE_LIMIT");
        if (!e || !*e) return 240.0;
        try { return std::stod(e); } catch (...) { return 240.0; }
    }();
    if (per_min <= 0.0) return true;   // disabled
    static RateLimiter limiter(per_min, per_min / 60.0);
    return limiter.tryAcquire(tool, nowSeconds());
}

} // namespace icmg::core
