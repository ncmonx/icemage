#pragma once
// Wake-up briefing formatting helpers (pure, testable).
// - iconLegend(): one-line key for the typed-icon vocabulary used in the
//   Decisions section (same icons as recall_index.hpp::iconFor).
// - briefingCostFooter(): "~N tok briefing" so the agent sees what the wake-up
//   bundle costs in context — claude-mem token-cost-visibility idea.
//
// Kept header-only + DB-free so the format is unit-tested in isolation, the
// same pattern as recall_index.hpp / private_filter.hpp.

#include "../core/token_budget.hpp"
#include <string>
#include <sstream>

namespace icmg::cli {

// Compact one-line legend. Mirrors the 6 icons iconFor() can emit in v1:
//   🟤 decision  🔴 critical/gotcha  🟡 fix/bug  🟣 research  🎯 plan  🔵 other
inline std::string iconLegend() {
    return std::string(
        "legend: \xF0\x9F\x9F\xA4 decision  \xF0\x9F\x94\xB4 gotcha  "
        "\xF0\x9F\x9F\xA1 fix  \xF0\x9F\x9F\xA3 research  "
        "\xF0\x9F\x8E\xAF plan  \xF0\x9F\x94\xB5 other");
}

// "~N tok briefing" footer for the given briefing text.
inline std::string briefingCostFooter(const std::string& briefing) {
    std::ostringstream os;
    os << "~" << core::estimateTokens(briefing) << " tok briefing";
    return os.str();
}

} // namespace icmg::cli
