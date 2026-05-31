#pragma once
// M8 T3: token budget — lightweight estimation + pre-flight gate.
// Technique from claude-code tokenEstimation.ts: bytes/4 heuristic.
// Non-blocking: estimation only, never throws.

#include <string>
#include <cstdint>
#include <cstdlib>

namespace icmg::core {

constexpr int BYTES_PER_TOKEN = 4; // conservative UTF-8 heuristic

// Estimate token count from byte length. Conservative (rounds up).
inline int64_t estimateTokens(std::size_t bytes) noexcept {
    return static_cast<int64_t>((bytes + BYTES_PER_TOKEN - 1) / BYTES_PER_TOKEN);
}

// Estimate tokens from string content.
inline int64_t estimateTokens(const std::string& s) noexcept {
    return estimateTokens(s.size());
}

// Check if content fits within token budget. Returns true if safe.
inline bool withinBudget(const std::string& s, int64_t max_tokens) noexcept {
    return estimateTokens(s) <= max_tokens;
}

// Get token budget from env (ICMG_TOKEN_BUDGET), or default.
inline int64_t tokenBudget(int64_t default_budget = 8192) noexcept {
    const char* env = std::getenv("ICMG_TOKEN_BUDGET");
    if (env && *env) {
        try { return std::stoll(env); } catch (...) {}
    }
    return default_budget;
}

} // namespace icmg::core
