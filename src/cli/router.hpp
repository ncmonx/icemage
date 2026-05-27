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

}  // namespace icmg::cli
