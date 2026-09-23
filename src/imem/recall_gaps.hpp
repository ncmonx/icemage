// 2026-08-25 brain v2.22 #2: retrieval-failure ledger (Mem0 production insight).
// A recall query that returned nothing (or almost nothing) is a SIGNAL: the
// agent needed knowledge the brain does not hold. Surfacing the recurring ones
// turns silent misses into an actionable "store this" checklist.
// Pure functions, no IO/DB/LLM -- the cmd layer feeds query-history rows in.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
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

enum class GapKind { Missed, Evicted };

struct RecallGap {
    std::string query;
    int         asks    = 1;
    int64_t     last_ts = 0;
    // IDE-D (2026-09-23, arXiv 2609.08279 "What Eviction Destroys"): an empty
    // recall has two OPPOSITE remedies -- store it (never held: Missed) or
    // restore it (held but soft-deleted: Evicted). Say which.
    GapKind     kind       = GapKind::Missed;
    int64_t     evicted_id = 0;   // strongest-matching deleted node (Evicted)
};

// One soft-deleted memory row (id + topic/content text to match against).
struct DeletedMemRow {
    int64_t     id = 0;
    std::string text;
};

// A query too short to be a real knowledge ask (noise like "a", "ok").
inline bool isNoiseQuery(const std::string& q) {
    int alnum = 0;
    for (char c : q)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            ++alnum;
    return alnum < 4;
}

namespace gap_detail {

// Lowercased word tokens of length >= 4 (short words carry no signal:
// "the", "fix", "for" would create false eviction matches).
inline std::set<std::string> strongTokens(const std::string& s) {
    std::set<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            if (cur.size() >= 4) out.insert(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 4) out.insert(cur);
    return out;
}

}  // namespace gap_detail

// Flag queries whose result_count <= max_results (default 0 = only true
// misses). Strongest first: more asks, then newer. Capped at max_out.
// `deleted` (optional): soft-deleted memory corpus -- a gap sharing >= 2
// strong tokens with a deleted node is classified Evicted (recoverable via
// `icmg memory restore`), everything else Missed (store new knowledge).
inline std::vector<RecallGap> findRecallGaps(const std::vector<GapQueryRow>& rows,
                                             int max_results = 0,
                                             int max_out = 25,
                                             const std::vector<DeletedMemRow>* deleted = nullptr) {
    std::vector<RecallGap> out;
    for (const auto& r : rows) {
        if (r.result_count > max_results) continue;
        if (isNoiseQuery(r.query)) continue;
        RecallGap g{r.query, r.asks, r.last_ts};
        if (deleted && !deleted->empty()) {
            const auto qtok = gap_detail::strongTokens(r.query);
            size_t best = 0;
            for (const auto& d : *deleted) {
                const auto dtok = gap_detail::strongTokens(d.text);
                size_t shared = 0;
                for (const auto& t : qtok) shared += dtok.count(t);
                if (shared >= 2 && shared > best) {   // >=2 real tokens = evidence
                    best = shared;
                    g.kind = GapKind::Evicted;
                    g.evicted_id = d.id;
                }
            }
        }
        out.push_back(std::move(g));
    }
    std::sort(out.begin(), out.end(), [](const RecallGap& a, const RecallGap& b) {
        if (a.asks != b.asks) return a.asks > b.asks;
        return a.last_ts > b.last_ts;
    });
    if ((int)out.size() > max_out) out.resize(max_out);
    return out;
}

} // namespace icmg::imem
