// TDD (2026-06-15): wake-up typed-icon legend + token-cost footer.
// Spec: claude-mem ideas (decisions-research) — surface the typed-icon vocab in
// the briefing + show how many tokens the briefing itself costs.
// Pure-function layer (mirrors recall_index.hpp) so the legend text + cost
// footer are unit-testable without running the binary / a DB.
// Failing FIRST: src/cli/wakeup_format.hpp does not exist yet.

#include "../test_main.hpp"
#include "../../src/cli/wakeup_format.hpp"

#include <string>

using icmg::cli::iconLegend;
using icmg::cli::briefingCostFooter;
using icmg::cli::newestHandoffTs;

// 1. Legend lists the typed-icon vocabulary (at least decision + gotcha + plan).
TEST("wakeup-format: iconLegend lists the typed-icon vocabulary") {
    std::string l = iconLegend();
    ASSERT_CONTAINS(l, "\xF0\x9F\x9F\xA4");   // 🟤 decision
    ASSERT_CONTAINS(l, "\xF0\x9F\x94\xB4");   // 🔴 gotcha
    ASSERT_CONTAINS(l, "\xF0\x9F\x8E\xAF");   // 🎯 plan
    // single line (legend is a compact one-liner), no trailing crash
    ASSERT_TRUE(!l.empty());
}

// 2. Cost footer reports a ~token estimate of the briefing text it is given.
TEST("wakeup-format: briefingCostFooter reports a ~token estimate") {
    std::string briefing(400, 'a');  // ~100 tok
    std::string f = briefingCostFooter(briefing);
    ASSERT_CONTAINS(f, "~");
    ASSERT_CONTAINS(f, "tok");
}

// 3. Footer token estimate grows with briefing length (monotonic).
TEST("wakeup-format: cost footer token estimate grows with briefing length") {
    auto tokOf = [](const std::string& line) -> int {
        auto p = line.find('~');
        if (p == std::string::npos) return -1;
        int v = 0; ++p;
        while (p < line.size() && line[p] >= '0' && line[p] <= '9') { v = v*10 + (line[p]-'0'); ++p; }
        return v;
    };
    int small = tokOf(briefingCostFooter(std::string(40, 'a')));
    int big   = tokOf(briefingCostFooter(std::string(800, 'a')));
    ASSERT_TRUE(small > 0);
    ASSERT_TRUE(big > small);
}

// 4. newestHandoffTs picks the max timestamp — a frozen legacy remember.md must
//    not shadow a newer recent.md/now.md (the 25-day-bogus-gap bug, 2026-07-05).
TEST("wakeup-format: newestHandoffTs picks the newest handoff, not the legacy one") {
    // remember.md frozen, recent.md fresh -> newest wins.
    ASSERT_EQ(newestHandoffTs({1000, 5000, 3000}), (int64_t)5000);
    // order independence
    ASSERT_EQ(newestHandoffTs({5000, 1000}), (int64_t)5000);
}

// 5. Absent/unreadable candidates (<= 0) are ignored; empty -> 0.
TEST("wakeup-format: newestHandoffTs ignores absent candidates") {
    ASSERT_EQ(newestHandoffTs({0, 0, 4200}), (int64_t)4200);
    ASSERT_EQ(newestHandoffTs({}), (int64_t)0);
    ASSERT_EQ(newestHandoffTs({-1, 0}), (int64_t)0);
}
