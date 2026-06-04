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
    int         pinned      = 0;  // Phase 75: 1 = decision-anchor; 10× recall boost
    std::string git_sha;          // Phase 15: git commit SHA at store time (short, may be empty)
    std::string source      = "unknown";  // provenance: who/what supplied this info

    // Computed at query time — not stored in DB
    double      score       = 0.0;
    double      bm25_score  = 0.0;
    double      recency     = 0.0;
    double      freq_score  = 0.0;
    double      importance_mult = 0.0;
    double      feedback_bias = 1.0;   // Phase 27: 0.8 .. 1.2 from avg user score
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
    // v1.21.9 (M2): accept long forms in addition to the legacy abbreviations.
    if (name == "low")                              return 0;
    if (name == "med" || name == "medium")          return 1;
    if (name == "high")                             return 2;
    if (name == "crit" || name == "critical")       return 3;
    return 1; // default: med
}

// v1.21.9 (M2): tier-aware decay-rate multiplier. Plan values:
//   critical = 0   (never decays)
//   high     = 0.5 (half-life 180d via ageDecay)
//   medium   = 1.0 (current half-life 90d)
//   low      = 2.0 (half-life 45d)
// Applied as a multiplier on the ageDecay rate constant (λ).
inline double importanceDecayMultiplier(int importance) {
    switch (importance) {
        case 3:  return 0.0;  // critical — frozen
        case 2:  return 0.5;
        case 0:  return 2.0;
        default: return 1.0;  // medium / unknown
    }
}

} // namespace icmg::imem
