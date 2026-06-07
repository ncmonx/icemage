// 2026-06-07: context-budget gauge (pure helpers).
#include "../test_main.hpp"
#include "../../src/cli/context_budget.hpp"
using namespace icmg::cli;

static const std::string USAGE =
  R"("usage":{"input_tokens":124,"cache_creation_input_tokens":3881,"cache_read_input_tokens":562599,"output_tokens":458})";

TEST("context_budget: pct math + clamps") {
    auto b = computeBudget(566604, 1000000);
    ASSERT_EQ(b.pctUsed, 56); ASSERT_EQ(b.pctLeft, 44);
    auto z = computeBudget(0, 1000000);     ASSERT_EQ(z.pctUsed, 0);   ASSERT_EQ(z.pctLeft, 100);
    auto o = computeBudget(2000000, 1000000); ASSERT_EQ(o.pctUsed, 100); ASSERT_EQ(o.pctLeft, 0);
    auto n = computeBudget(100, 0);          ASSERT_EQ(n.pctUsed, 0);   ASSERT_EQ(n.pctLeft, 100); // no limit safe
}

TEST("context_budget: extractLL exact key (not substring)") {
    ASSERT_EQ(extractLL(USAGE, "input_tokens"), 124LL);                   // not the cache_* ones
    ASSERT_EQ(extractLL(USAGE, "cache_creation_input_tokens"), 3881LL);
    ASSERT_EQ(extractLL(USAGE, "cache_read_input_tokens"), 562599LL);
    ASSERT_EQ(extractLL(USAGE, "missing"), 0LL);
}

TEST("context_budget: contextTokens sums 3 inputs (excludes output)") {
    ASSERT_EQ(contextTokensFromUsageLine(USAGE), 566604LL);  // 124+3881+562599, not +458
}

TEST("context_budget: format string") {
    ASSERT_CONTAINS(formatBudget(computeBudget(566604, 1000000)), "56% used");
    ASSERT_CONTAINS(formatBudget(computeBudget(566604, 1000000)), "44% left");
}
