// 2026-06-06: icmg agent native-local advisory backend decision helpers.
#include "../test_main.hpp"
#include "../../src/cli/agent_local.hpp"

using namespace icmg::cli;

TEST("agent_local: refuse local + exec (advisory-only)") {
    auto d = agentLocalDecision(/*premium_available=*/false, /*explicit_local=*/false,
                                /*exec=*/true);
    ASSERT_FALSE(d.use_local);
    ASSERT_TRUE(d.refuse_exec);
    ASSERT_CONTAINS(d.reason, "exec");
}

TEST("agent_local: local advisory when no premium, no exec") {
    auto d = agentLocalDecision(false, false, false);
    ASSERT_TRUE(d.use_local);
    ASSERT_FALSE(d.refuse_exec);
}

TEST("agent_local: premium present + no explicit -> external CLI (no local)") {
    auto d = agentLocalDecision(true, false, false);
    ASSERT_FALSE(d.use_local);
    ASSERT_FALSE(d.refuse_exec);
}

TEST("agent_local: explicit_local advisory even with premium") {
    auto d = agentLocalDecision(true, true, false);
    ASSERT_TRUE(d.use_local);
}

TEST("agent_local: explicit_local + exec still refused (premium present)") {
    auto d = agentLocalDecision(true, true, true);
    ASSERT_TRUE(d.refuse_exec);
    ASSERT_FALSE(d.use_local);
}

TEST("agent_local: truncatePrompt clamps to window budget") {
    std::string big(40000, 'x');           // ~10k tokens > 8k window
    bool warned = false;
    std::string out = truncatePromptToWindow(big, /*max_tokens=*/8000, warned);
    ASSERT_TRUE(warned);
    ASSERT_TRUE(out.size() < big.size());
    ASSERT_EQ(out.size(), static_cast<std::size_t>(8000) * 4);
}

TEST("agent_local: truncatePrompt leaves small prompt unchanged") {
    std::string small = "short task";
    bool warned = true;
    std::string out = truncatePromptToWindow(small, 8000, warned);
    ASSERT_FALSE(warned);
    ASSERT_EQ(out, small);
}
