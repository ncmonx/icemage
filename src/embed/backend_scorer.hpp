#pragma once
// M8 T8: provider/backend routing scorer.
// Technique from openclaude smart_router.py:
// score = reliability*0.3 + (1-normalized_latency)*0.5 + (1-normalized_cost)*0.2
// Selects fastest/cheapest embedding backend dynamically.

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <algorithm>

namespace icmg::embed {

struct BackendScore {
    std::string name;
    double      latency_ms  = 0.0; // last observed latency
    double      error_rate  = 0.0; // fraction of failed calls [0,1]
    double      cost_weight = 1.0; // relative cost (1.0 = normal, 0.5 = half cost)
    int         call_count  = 0;

    // Score: higher = better. Latency dominates (50%), reliability 30%, cost 20%.
    double score(double max_latency_ms = 5000.0) const noexcept {
        double norm_lat  = max_latency_ms > 0 ? std::min(latency_ms / max_latency_ms, 1.0) : 0.0;
        double norm_cost = std::min(cost_weight, 1.0);
        return (1.0 - error_rate) * 0.3
             + (1.0 - norm_lat)   * 0.5
             + (1.0 - norm_cost)  * 0.2;
    }

    void record(double latency, bool success) noexcept {
        ++call_count;
        // Exponential moving average (alpha=0.3)
        latency_ms = call_count == 1 ? latency : latency_ms * 0.7 + latency * 0.3;
        error_rate = call_count == 1 ? (success ? 0.0 : 1.0)
                                     : error_rate * 0.7 + (success ? 0.0 : 1.0) * 0.3;
    }
};

// Select backend with highest score. Returns index into `backends`.
inline int selectBest(const std::vector<BackendScore>& backends) noexcept {
    if (backends.empty()) return -1;
    int best = 0;
    double best_score = backends[0].score();
    for (int i = 1; i < (int)backends.size(); ++i) {
        double s = backends[i].score();
        if (s > best_score) { best_score = s; best = i; }
    }
    return best;
}

} // namespace icmg::embed
