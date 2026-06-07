// v1.31.0 B1.5: smart router. Decides REGEX vs LLM_LOCAL vs LLM_CLOUD vs CACHE
// per call. Sub-ms (~100 us p99) — pure stateless function over a CallContext.
//
// Layered decision:
//   1. HARD RULES (always enforced):
//      - hot path -> REGEX, full stop
//      - user opt-out file present -> never LLM_LOCAL
//      - prior result cached -> CACHE
//   2. HEURISTICS:
//      - input < small_threshold tokens -> REGEX (LLM overhead wins not earned)
//      - high RAM pressure (<min_ram) -> REGEX
//      - LLM not loaded yet AND request is hot -> REGEX (avoid cold-load on hot)
//   3. ADAPTIVE (telemetry-driven):
//      - p95 wall_ms over last 10 > 5000 ms -> regex cooldown 1 hr
//      - error_rate over last 10 > 0.20    -> disable 1 hr
//      - cold_load_fail_count >= 2         -> session-disable
//
// CI-lint guards (see `tools/lint_no_llm_in_hot.sh`): routeFor must NEVER
// be called from bundle/run/tkil/hook_cmd hot-path TUs. Linter greps for
// `smart_router.hpp` includes in those dirs and fails build if found.
//
// Used by:
//   - icmg-service warm-pool dispatcher (B3)
//   - PreCompact (C1), pack --rerank (B4), ask --backend=local
#pragma once
#include <cstdint>
#include <string>

namespace icmg::llm {

enum class PathTier {
    HOT,    // PreToolUse:Read/Edit/Bash, UserPromptSubmit; never LLM
    WARM,   // pack rerank, ask --backend=local; LLM async OK
    COLD    // PreCompact summarize, daily aggregate; LLM full-quality OK
};

enum class Route {
    REGEX,        // fallback / forced
    LLM_LOCAL,    // run via LlamaRunner / warm-pool
    LLM_CLOUD,    // (reserved for future Claude API offload)
    CACHE         // result already memoized — caller fetches by key
};

struct CallContext {
    PathTier    tier              = PathTier::WARM;
    std::string kind;             // e.g. "compact", "rerank", "intent", "ask"
    std::size_t input_tokens_est  = 0;
    bool        result_cached     = false;
    bool        llm_loaded        = false;   // warm-pool has model in RAM
    bool        user_disabled     = false;   // ~/.icmg/llm/disabled present
    bool        build_has_llama   = false;   // LlamaRunner::available()
    // 2026-06-06 no-premium routing: local LLM is reserved for executions with
    // no premium LLM (Claude) present, or when the caller explicitly asks local.
    bool        premium_available = true;    // Claude/premium present this execution
    bool        explicit_local    = false;   // user chose local (ask --backend=local, chat)
};

struct RouteDecision {
    Route       route = Route::REGEX;
    const char* reason = "default";  // for debug logging / `icmg llm status`
};

// Pure function. No I/O beyond Telemetry::stats() snapshot (atomic-cheap).
// Sub-ms p99 by design — caller may invoke per hook fire.
RouteDecision routeFor(const CallContext& ctx);

} // namespace icmg::llm
