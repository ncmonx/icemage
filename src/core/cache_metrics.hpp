#pragma once
// M8 T9: unified cache metrics normalizer.
// Technique from openclaude cacheMetrics.ts: normalize hit rates across layers.

#include <cstdint>
#include <string>
#include <vector>

namespace icmg::core {

struct CacheLayerMetrics {
    std::string name;
    int64_t     hits      = 0;
    int64_t     misses    = 0;
    int64_t     entries   = 0;
    int64_t     bytes     = 0;
    int64_t     evictions = 0;
    bool        supported = true;

    double hitRate() const noexcept {
        int64_t total = hits + misses;
        if (total == 0) return -1.0;
        return static_cast<double>(hits) / static_cast<double>(total);
    }
};

inline double aggregateHitRate(const std::vector<CacheLayerMetrics>& layers) noexcept {
    int64_t hits = 0, total = 0;
    for (const auto& l : layers) {
        if (!l.supported) continue;
        hits  += l.hits;
        total += l.hits + l.misses;
    }
    if (total == 0) return -1.0;
    return static_cast<double>(hits) / static_cast<double>(total);
}

inline std::string fmtHitRate(double rate) {
    if (rate < 0.0) return "n/a";
    int pct = static_cast<int>(rate * 1000.0 + 0.5);
    return std::to_string(pct / 10) + "." + std::to_string(pct % 10) + "%";
}

} // namespace icmg::core
