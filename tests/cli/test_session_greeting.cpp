// TDD: gap-aware session greeting. Clearing the conversation a few minutes
// apart must read as CONTINUE, not a fresh "good morning".
#include "../test_main.hpp"
#include "../../src/cli/session_greeting.hpp"

using namespace icmg::cli;

static constexpr int64_t NOW = 1700000000;

TEST("greeting: no handoff timestamp -> unknown") {
    auto h = computeGreetingHint(NOW, 0, /*haveLast=*/false);
    ASSERT_EQ(h.mode, std::string("unknown"));
    ASSERT_EQ(h.gapSec, (int64_t)0);
}

TEST("greeting: 9 minutes apart -> continue") {
    auto h = computeGreetingHint(NOW, NOW - 9 * 60, true);
    ASSERT_EQ(h.mode, std::string("continue"));
    ASSERT_EQ(h.gapSec, (int64_t)(9 * 60));
}

TEST("greeting: 7 hours apart -> continue") {
    auto h = computeGreetingHint(NOW, NOW - 7 * 3600, true);
    ASSERT_EQ(h.mode, std::string("continue"));
}

TEST("greeting: 9 hours apart -> fresh") {
    auto h = computeGreetingHint(NOW, NOW - 9 * 3600, true);
    ASSERT_EQ(h.mode, std::string("fresh"));
}

TEST("greeting: exactly at threshold -> fresh") {
    auto h = computeGreetingHint(NOW, NOW - kFreshSessionGapSec, true);
    ASSERT_EQ(h.mode, std::string("fresh"));
}

TEST("greeting: clock skew (future) -> gap clamped, continue") {
    auto h = computeGreetingHint(NOW, NOW + 500, true);
    ASSERT_EQ(h.gapSec, (int64_t)0);
    ASSERT_EQ(h.mode, std::string("continue"));
}

TEST("greeting: gap label formatting") {
    ASSERT_EQ(formatGap(30), std::string("<1m"));
    ASSERT_EQ(formatGap(9 * 60), std::string("~9m"));
    ASSERT_EQ(formatGap(3 * 3600), std::string("~3h"));
    ASSERT_EQ(formatGap(86400 + 2 * 3600), std::string("~1d2h"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
