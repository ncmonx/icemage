#pragma once
#include <string>
#include <cstdint>

namespace icmg::imem {

struct MemoryNode {
    int64_t     id          = 0;
    std::string topic;
    std::string content;
    std::string keywords;   // comma-separated
    int         importance  = 1;  // 0=low 1=med 2=high 3=critical
    int         frequency   = 1;
    int64_t     last_used   = 0;  // unix epoch
    int64_t     created_at  = 0;  // unix epoch
    int64_t     expires_at  = 0;  // unix epoch, 0 = never
    int64_t     deleted_at  = 0;  // unix epoch, 0 = not deleted
    std::string zone        = "default";  // Phase 17: subsystem partitioning

    // Computed at query time — not stored in DB
    double      score       = 0.0;
    double      bm25_score  = 0.0;
    double      recency     = 0.0;
    double      freq_score  = 0.0;
    double      importance_mult = 0.0;
};

// Importance level helpers
inline const char* importanceName(int imp) {
    switch (imp) {
        case 0: return "low";
        case 1: return "med";
        case 2: return "high";
        case 3: return "crit";
        default: return "med";
    }
}

inline int importanceFromName(const std::string& name) {
    if (name == "low")  return 0;
    if (name == "high") return 2;
    if (name == "crit") return 3;
    return 1; // default: med
}

} // namespace icmg::imem
