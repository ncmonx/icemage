// 2026-06-06: smart_router no-premium gate. Local LLM fires only when
// (!premium_available || explicit_local), subject to all existing hard-rules.

#include "../test_main.hpp"
#include "../../src/llm/smart_router.hpp"

using namespace icmg::llm;

static CallContext baseCtx() {
    CallContext c;
    c.tier             = PathTier::WARM;
    c.kind             = "agent";
    c.input_tokens_est = 2000;     // above small-input threshold
    c.result_cached    = false;
    c.llm_loaded       = true;     // warm model present
    c.user_disabled    = false;
    c.build_has_llama  = true;
    return c;
}

TEST("router: premium present + not explicit -> REGEX (new gate)") {
    CallContext c = baseCtx();
    c.premium_available = true;
    c.explicit_local    = false;
    ASSERT_TRUE(routeFor(c).route == Route::REGEX);
}

TEST("router: no premium -> LLM_LOCAL") {
    CallContext c = baseCtx();
    c.premium_available = false;
    ASSERT_TRUE(routeFor(c).route == Route::LLM_LOCAL);
}

TEST("router: premium present + explicit_local -> LLM_LOCAL") {
    CallContext c = baseCtx();
    c.premium_available = true;
    c.explicit_local    = true;
    ASSERT_TRUE(routeFor(c).route == Route::LLM_LOCAL);
}

TEST("router: hot path still REGEX even when no premium") {
    CallContext c = baseCtx();
    c.premium_available = false;
    c.tier = PathTier::HOT;
    ASSERT_TRUE(routeFor(c).route == Route::REGEX);
}

TEST("router: build lacks llama -> REGEX regardless of premium") {
    CallContext c = baseCtx();
    c.premium_available = false;
    c.build_has_llama   = false;
    ASSERT_TRUE(routeFor(c).route == Route::REGEX);
}

TEST("router: cache hit wins over no-premium gate") {
    CallContext c = baseCtx();
    c.premium_available = false;
    c.result_cached     = true;
    ASSERT_TRUE(routeFor(c).route == Route::CACHE);
}

TEST("router: precompact COLD no-premium -> LLM_LOCAL; with premium -> REGEX") {
    CallContext c;
    c.tier = PathTier::COLD; c.kind = "compact"; c.input_tokens_est = 4000;
    c.build_has_llama = true; c.llm_loaded = true;
    c.premium_available = false; c.explicit_local = false;
    ASSERT_TRUE(routeFor(c).route == Route::LLM_LOCAL);
    c.premium_available = true;
    ASSERT_TRUE(routeFor(c).route == Route::REGEX);
}
