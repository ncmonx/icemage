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
#include <cstdint>
#include <vector>

namespace icmg::cli {

// Pick the newest (max) handoff timestamp from a set of candidates. A candidate
// <= 0 means "absent/unreadable" and is ignored. Returns 0 if none are valid.
// The legacy `remember.md` can freeze (stopped being written 2026-06-10) while
// the active handoff moved to recent.md / now.md; taking the max across all of
// them keeps a fresh continuation from being mislabeled as a stale gap.
inline int64_t newestHandoffTs(const std::vector<int64_t>& candidates) {
    int64_t best = 0;
    for (int64_t ts : candidates) {
        if (ts > best) best = ts;
    }
    return best;
}

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
