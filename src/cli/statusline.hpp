#pragma once
// Compact one-line status for Claude Code's `statusLine` integration. Makes the
// otherwise-invisible context budget visible EVERY turn in the CC status bar:
//   icmg opus-4-8 | ctx 56% | 567K/1.0M
// Reuses the per-model honest budget (context_budget.hpp). Pure + header-only
// so the formatting is unit-testable; the command wires stdin -> these helpers.
#include "context_budget.hpp"
#include <string>

namespace icmg::cli {

// Human-readable token count: <1000 -> raw, <1M -> "NK", else "N.NM".
inline std::string humanTok(long long n) {
    if (n < 0) n = 0;
    if (n < 1000) return std::to_string(n);
    if (n < 1000000) return std::to_string((n + 500) / 1000) + "K";
    long long m10 = (n + 50000) / 100000;  // tenths of a million
    return std::to_string(m10 / 10) + "." + std::to_string(m10 % 10) + "M";
}

// Short model name for the status bar: drop a vendor "prefix/" and a leading
// "claude-". "" for empty / synthetic turns.
inline std::string modelShortName(const std::string& model) {
    if (model.empty() || model.find("synthetic") != std::string::npos) return "";
    std::string s = model;
    auto slash = s.find_last_of('/');
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const std::string pfx = "claude-";
    if (s.rfind(pfx, 0) == 0) s = s.substr(pfx.size());
    return s;
}

// Build the status line. `modelShort` may be empty (omitted then).
inline std::string formatStatusline(const BudgetInfo& b, const std::string& modelShort) {
    std::string s = "icmg";
    if (!modelShort.empty()) s += " " + modelShort;
    s += " | ctx " + std::to_string(b.pctUsed) + "%";
    if (b.limit > 0) s += " | " + humanTok(b.used) + "/" + humanTok(b.limit);
    return s;
}

}  // namespace icmg::cli
