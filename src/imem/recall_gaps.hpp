// 2026-08-25 brain v2.22 #2: retrieval-failure ledger (Mem0 production insight).
// A recall query that returned nothing (or almost nothing) is a SIGNAL: the
// agent needed knowledge the brain does not hold. Surfacing the recurring ones
// turns silent misses into an actionable "store this" checklist.
// Pure functions, no IO/DB/LLM -- the cmd layer feeds query-history rows in.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::imem {

// One aggregated query-history row (cmd layer groups repeats before calling).
struct GapQueryRow {
    std::string query;
    int         result_count = 0;   // results the recall returned (max seen)
    int64_t     last_ts      = 0;   // most recent ask, unix epoch
    int         asks         = 1;   // how many times this query was logged
};

struct RecallGap {
    std::string query;
    int         asks    = 1;
    int64_t     last_ts = 0;
};

// A query too short to be a real knowledge ask (noise like "a", "ok").
inline bool isNoiseQuery(const std::string& q) {
    int alnum = 0;
    for (char c : q)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            ++alnum;
    return alnum < 4;
}

// Flag queries whose result_count <= max_results (default 0 = only true
// misses). Strongest first: more asks, then newer. Capped at max_out.
inline std::vector<RecallGap> findRecallGaps(const std::vector<GapQueryRow>& rows,
                                             int max_results = 0,
                                             int max_out = 25) {
    std::vector<RecallGap> out;
    for (const auto& r : rows) {
        if (r.result_count > max_results) continue;
        if (isNoiseQuery(r.query)) continue;
        out.push_back({r.query, r.asks, r.last_ts});
    }
    std::sort(out.begin(), out.end(), [](const RecallGap& a, const RecallGap& b) {
        if (a.asks != b.asks) return a.asks > b.asks;
        return a.last_ts > b.last_ts;
    });
    if ((int)out.size() > max_out) out.resize(max_out);
    return out;
}

} // namespace icmg::imem
