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
