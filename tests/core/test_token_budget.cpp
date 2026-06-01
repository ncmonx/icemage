// M8 T3: token budget estimation tests.
#include "../test_main.hpp"
#include "../../src/core/token_budget.hpp"
#include <string>

TEST("token_budget: estimateTokens bytes/4 heuristic") {
    ASSERT_EQ(icmg::core::estimateTokens(0), 0);
    ASSERT_EQ(icmg::core::estimateTokens(4), 1);
    ASSERT_EQ(icmg::core::estimateTokens(5), 2); // rounds up
    ASSERT_EQ(icmg::core::estimateTokens(8), 2);
    ASSERT_EQ(icmg::core::estimateTokens(1000), 250);
}

TEST("token_budget: estimateTokens from string") {
    std::string s(100, 'a');
    ASSERT_EQ(icmg::core::estimateTokens(s), 25);
}

TEST("token_budget: withinBudget true when small") {
    std::string small(40, 'x'); // 10 tokens
    ASSERT_TRUE(icmg::core::withinBudget(small, 100));
    ASSERT_TRUE(icmg::core::withinBudget(small, 10));
}

TEST("token_budget: withinBudget false when oversized") {
    std::string big(400, 'x'); // 100 tokens
    ASSERT_FALSE(icmg::core::withinBudget(big, 50));
}

TEST("token_budget: default budget is 8192") {
    ASSERT_EQ(icmg::core::tokenBudget(), 8192);
}
