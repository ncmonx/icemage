#pragma once
#include <string>

namespace icmg::cli {

enum class Route { LOCAL, CLOUD, CACHE };

struct RouteDecision {
    Route route = Route::CLOUD;
    double confidence = 0.0;
    std::string reason;
};

// Classify prompt -> routing decision based on length, keywords, tool verbs.
// Pure fn; no IO. Cache lookup deferred to caller (see Sub-B2).
RouteDecision classifyPrompt(const std::string& text);

// v1.61 F10: confidence scoring. Central bands so router, recall, and fuzzy
// agree on what "low" means; callers surface a "[low-confidence]" hint and
// (F6) decide whether to escalate an ambiguous route to the local LLM.
enum class ConfidenceBand { Low, Medium, High };

constexpr double kConfidenceLow  = 0.40;   // <  this => Low
constexpr double kConfidenceHigh = 0.75;   // >= this => High

inline ConfidenceBand confidenceBand(double c) {
    if (c < kConfidenceLow)  return ConfidenceBand::Low;
    if (c < kConfidenceHigh) return ConfidenceBand::Medium;
    return ConfidenceBand::High;
}

inline bool isLowConfidence(double c) { return c < kConfidenceLow; }

inline const char* confidenceBandName(ConfidenceBand b) {
    switch (b) {
        case ConfidenceBand::Low:    return "low";
        case ConfidenceBand::Medium: return "medium";
        case ConfidenceBand::High:   return "high";
    }
    return "unknown";
}

}  // namespace icmg::cli
