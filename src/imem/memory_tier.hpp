#pragma once
// v2.0.0 externals (Tiered Memory): classify a memory node into hot/warm/cold by
// recency + frequency + importance. Pure + header-only so callers (memory list,
// recall ranking, eviction) share one definition. No schema change — uses the
// existing last_used / frequency / importance columns.
#include <string>
#include <cstring>
#include <cstdint>

namespace icmg::imem {

enum class MemTier { Hot, Warm, Cold };

// Thresholds (days). Critical importance (3) pins Hot regardless of age.
inline MemTier memoryTier(int64_t last_used, int frequency, int importance,
                          int64_t now) {
    if (importance >= 3) return MemTier::Hot;            // critical: never demote
    const int64_t DAY = 86400;
    int64_t age_days = last_used > 0 ? (now - last_used) / DAY : 1'000'000;

    if (age_days <= 2  || frequency >= 5) return MemTier::Hot;
    if (age_days <= 30 || frequency >= 2) return MemTier::Warm;
    return MemTier::Cold;
}

inline const char* memTierName(MemTier t) {
    switch (t) {
        case MemTier::Hot:  return "hot";
        case MemTier::Warm: return "warm";
        default:            return "cold";
    }
}

inline MemTier memTierFromName(const std::string& s) {
    if (s == "hot")  return MemTier::Hot;
    if (s == "cold") return MemTier::Cold;
    return MemTier::Warm;
}

// A1 wiring: gate predicate for tier-aware eviction (`memory prune --tier`).
// Empty filter = no gate (every node passes). Non-empty = pass only when the
// node's computed tier name equals the filter. Keeps the tier policy in one
// pure, testable place so prune/consolidate share the same "cold first" logic.
inline bool tierPasses(const std::string& tier_filter, int64_t last_used,
                       int frequency, int importance, int64_t now) {
    if (tier_filter.empty()) return true;
    return memTierName(memoryTier(last_used, frequency, importance, now)) == tier_filter;
}

}  // namespace icmg::imem
