// v2.0.0 C5: idle-compact advisor — nudge /compact at a natural idle moment when context
// fill is high, rate-limited by 10% band so it fires once per band, not every turn. Pure.
#include "../test_main.hpp"
#include "../../src/core/compact_advisor.hpp"
#include <string>

using namespace icmg::core;

TEST("idleCompactAdvice: below threshold -> no fire") {
    auto n = idleCompactAdvice(60, -1, 75);
    ASSERT_TRUE(!n.fire);
}

TEST("idleCompactAdvice: at/above threshold, new band -> fire") {
    auto n = idleCompactAdvice(78, -1, 75);
    ASSERT_TRUE(n.fire);
    ASSERT_TRUE(n.band == 7);            // floor(78/10)
}

TEST("idleCompactAdvice: same band already fired -> no refire") {
    auto n = idleCompactAdvice(79, 7, 75);  // lastFiredBand=7, still band 7
    ASSERT_TRUE(!n.fire);
}

TEST("idleCompactAdvice: next band up -> fire again") {
    auto n = idleCompactAdvice(88, 7, 75);  // band 8 > lastFired 7
    ASSERT_TRUE(n.fire);
    ASSERT_TRUE(n.band == 8);
}

TEST("idleCompactAdvice: message mentions compact + percent") {
    auto n = idleCompactAdvice(82, -1, 75);
    ASSERT_TRUE(n.message.find("compact") != std::string::npos);
    ASSERT_TRUE(n.message.find("82") != std::string::npos);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
