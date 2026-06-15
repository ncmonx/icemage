#pragma once
// savings_daily.hpp — pure rendering for the `icmg savings --daily` console view.
//
// The per-day saved-token AGGREGATION (reading telemetry tables + JSONL logs)
// lives in savings_cmd.cpp::aggregateDailySaved(). This header isolates the
// PURE formatting step so it is unit-testable without a DB: given a
// date -> saved-tokens map, render newest-first console rows.
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

// Render up to `maxRows` rows, newest date first. `by_day` keys are
// "YYYY-MM-DD" strings (lexicographic order == chronological order), so we
// iterate in reverse for newest-first. Rows look like:
//   "  2026-06-14        123456 tok saved"
inline std::vector<std::string> formatDailySavingsRows(
        const std::map<std::string, int64_t>& by_day, int maxRows) {
    std::vector<std::string> out;
    if (maxRows <= 0) return out;
    int shown = 0;
    for (auto it = by_day.rbegin(); it != by_day.rend(); ++it) {
        if (shown++ >= maxRows) break;
        std::ostringstream os;
        os << "  " << it->first << "  "
           << std::setw(12) << it->second << " tok saved";
        out.push_back(os.str());
    }
    return out;
}

} // namespace icmg::cli
