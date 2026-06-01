// v1.31.0 B5: LLM telemetry ring-buffer.
//
// In-process ring of recent LLM call outcomes (last N=100). Fed by:
//   - LlamaRunner::infer (warm/cold path)
//   - icmg-service warm-pool IPC handler (B3)
//
// Consumed by:
//   - smart router B1.5 adaptive layer (p95 wall_ms, error rate, cold-load fail count)
//   - `icmg llm bench` / `icmg llm status` (last-N summary)
//
// Design:
//   - Lock-protected (mutex); calls happen at warm/cold cadence (1-10/min),
//     mutex contention negligible.
//   - In-process only; warm-pool (B3) is a long-lived process so the ring
//     accumulates real signal. Foreground CLI calls populate their own
//     short-lived ring (still useful for adaptive decisions made WITHIN
//     a single CLI invocation when it issues multiple LLM calls).
//
// Counters tracked:
//   - last-10 p95 wall_ms (router degrade trigger: >5s -> 1 hr regex cooldown)
//   - last-10 error rate (>20% -> 1 hr disable)
//   - cold-load fail counter (>=2 in session -> session-disable)
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace icmg::llm {

struct CallSample {
    std::int64_t  unix_ms      = 0;     // wall-clock at completion
    std::string   kind;                 // "infer", "load", "rerank", "summarize"
    double        wall_ms      = 0.0;
    std::uint32_t tokens_in    = 0;
    std::uint32_t tokens_out   = 0;
    bool          ok           = false;
    bool          cold_load    = false; // true iff this was the initial load
    std::string   error;                // empty when ok
};

class Telemetry {
public:
    static Telemetry& instance();

    void push(const CallSample& s);

    // Snapshot last N samples (most recent first). Cheap copy.
    std::vector<CallSample> snapshot(std::size_t max_n = 100) const;

    struct Stats {
        std::size_t n              = 0;       // samples considered
        double      p50_wall_ms    = 0.0;
        double      p95_wall_ms    = 0.0;
        double      avg_tok_per_s  = 0.0;
        double      error_rate     = 0.0;     // 0..1
        std::uint32_t cold_load_fail_count = 0; // session-wide
    };

    // Derived stats over the most recent N samples (default 10 for adaptive
    // router decisions). N=0 -> all samples.
    Stats stats(std::size_t last_n = 10) const;

    // Reset everything (test hook / `icmg llm telemetry --reset`).
    void clear();

private:
    Telemetry() = default;
    mutable std::mutex          mu_;
    std::vector<CallSample>     ring_;          // append-only, capped via head_
    std::size_t                 head_     = 0;  // next write index when full
    bool                        full_     = false;
    static constexpr std::size_t kCap     = 100;
    std::uint32_t               cold_load_fail_ = 0;
};

} // namespace icmg::llm
