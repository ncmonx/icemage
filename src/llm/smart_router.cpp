// v1.31.0 B1.5: smart router impl. See smart_router.hpp.
#include "smart_router.hpp"
#include "telemetry.hpp"

#include <chrono>
#include <cstdint>

namespace icmg::llm {

namespace {

// Adaptive degradation thresholds.
constexpr double kAdaptP95WallMs   = 5000.0;     // > this -> regex cooldown 1 hr
constexpr double kAdaptErrorRate   = 0.20;       // > this -> disable 1 hr
constexpr std::uint32_t kAdaptColdLoadFailMax = 2; // >= -> session-disable

// Cooldown windows.
constexpr std::int64_t kCooldownMs = 60 * 60 * 1000; // 1 hr

// Heuristic thresholds.
constexpr std::size_t kSmallInputTokens = 64;   // below this regex always wins

// Session-scoped cooldown trackers. NOT cross-process; warm-pool (B3) and
// foreground CLI each have their own. Acceptable: telemetry within a
// process is what tunes that process.
std::int64_t g_regex_cooldown_until_ms = 0;
std::int64_t g_disable_until_ms        = 0;
bool         g_session_disabled        = false;

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

RouteDecision routeFor(const CallContext& ctx) {
    // ----- Layer 1: HARD RULES -----
    if (ctx.tier == PathTier::HOT)
        return { Route::REGEX, "hot-path forced regex (SLA <50ms)" };
    if (ctx.result_cached)
        return { Route::CACHE,  "cache hit" };
    if (!ctx.build_has_llama)
        return { Route::REGEX, "build lacks ICMG_USE_LLAMA" };
    if (ctx.user_disabled)
        return { Route::REGEX, "user opt-out (~/.icmg/llm/disabled)" };

    // ----- Layer 3a: session-disable (sticky) -----
    if (g_session_disabled)
        return { Route::REGEX, "session-disabled (cold-load fail x2)" };

    // ----- Layer 3b: timed cooldowns -----
    std::int64_t t = nowMs();
    if (t < g_disable_until_ms)
        return { Route::REGEX, "LLM disabled (1 hr — error rate >20%)" };
    if (t < g_regex_cooldown_until_ms)
        return { Route::REGEX, "regex cooldown (1 hr — p95 wall >5s)" };

    // ----- Layer 3c: refresh cooldowns from telemetry -----
    auto st = Telemetry::instance().stats(/*last_n=*/10);
    if (st.cold_load_fail_count >= kAdaptColdLoadFailMax) {
        g_session_disabled = true;
        return { Route::REGEX, "session-disabled (cold-load fail x2)" };
    }
    if (st.n >= 5) { // only adapt with enough signal
        if (st.error_rate > kAdaptErrorRate) {
            g_disable_until_ms = t + kCooldownMs;
            return { Route::REGEX, "LLM disabled (1 hr — error rate >20%)" };
        }
        if (st.p95_wall_ms > kAdaptP95WallMs) {
            g_regex_cooldown_until_ms = t + kCooldownMs;
            return { Route::REGEX, "regex cooldown (1 hr — p95 wall >5s)" };
        }
    }

    // ----- Layer 2: HEURISTICS -----
    if (ctx.input_tokens_est > 0 && ctx.input_tokens_est < kSmallInputTokens)
        return { Route::REGEX, "small input — LLM overhead not earned" };
    if (ctx.tier == PathTier::WARM && !ctx.llm_loaded)
        return { Route::REGEX, "warm path + cold model — regex this round" };

    // Default for warm/cold: use local LLM.
    return { Route::LLM_LOCAL, "default warm/cold -> LLM_LOCAL" };
}

} // namespace icmg::llm
