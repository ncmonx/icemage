// 2026-09-23: bench-recall --replay aggregation -- IDE-A from the landscape
// scan (DolphinBench, arXiv 2609.24971: judge memory by task outcome + COST).
// Replays real past recall queries; the cmd layer runs the recalls and feeds
// per-query rows here. Pure aggregation, no DB/IO/LLM.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::imem {

// One replayed query: what it found back then vs what it finds now.
struct ReplayRow {
    std::string query;
    int         past_hits    = 0;   // results when originally asked (history)
    int         now_hits     = 0;   // results from replaying it today
    int64_t     result_chars = 0;   // combined chars of today's top results
};

struct ReplayStats {
    int    total = 0;               // queries replayed
    int    hits  = 0;               // still answerable (now_hits > 0)
    double hit_rate = 0.0;          // hits / total
    std::vector<std::string> regressed;  // had results before, none now
    int    avg_answer_tokens = 0;   // ~chars/4 mean over answered queries
    int64_t total_answer_tokens = 0;
};

// Aggregate replay rows into headline numbers. Token estimate: chars/4 (the
// same heuristic the savings ledger uses -- comparable, not exact).
inline ReplayStats computeReplayStats(const std::vector<ReplayRow>& rows) {
    ReplayStats s;
    s.total = (int)rows.size();
    int answered = 0;
    for (const auto& r : rows) {
        if (r.now_hits > 0) {
            ++s.hits;
            ++answered;
            s.total_answer_tokens += r.result_chars / 4;
        } else if (r.past_hits > 0) {
            s.regressed.push_back(r.query);
        }
    }
    if (s.total > 0) s.hit_rate = (double)s.hits / (double)s.total;
    if (answered > 0)
        s.avg_answer_tokens = (int)(s.total_answer_tokens / answered);
    return s;
}

}  // namespace icmg::imem
