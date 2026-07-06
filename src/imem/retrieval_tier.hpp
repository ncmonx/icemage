#pragma once
// Two-tier retrieval scheduling (feature #3).
//
// icmg's recall has a cheap tier (BM25 lexical, instant) and a deep tier
// (ONNX/semantic embedding, ~5-6s cold-load + per-query embed). Always paying
// the deep tier is wasteful; skipping it loses conceptual matches. This pure,
// deterministic classifier decides -- from the CHEAP pass outcome alone --
// whether a query warrants escalation to the deep tier.
//
// Signal (all derived from the free BM25 pass, no extra compute):
//   top_score       best composite score among candidates
//   strong_hits     # candidates whose score clears the strong-score floor
//   candidate_count # candidates the lexical pass returned at all
//   query_tokens    token count of the query (conceptual vs keyword lookup)
//
// Rule of thumb: if the cheap pass is CONFIDENT (a strong top hit exists) keep
// it cheap; if it's WEAK or EMPTY, escalate so semantic recall can try. Longer
// natural-language questions with only a marginal lexical hit also escalate,
// since those are exactly where embeddings beat BM25.

#include <string>

namespace icmg::imem {

enum class RetrievalTier { Cheap, Deep };

struct QueryTierSignal {
    double top_score       = 0.0;
    int    strong_hits     = 0;
    int    candidate_count = 0;
    int    query_tokens    = 0;
};

struct TierThresholds {
    double strong_score = 1.0;   // score at/above which a hit counts as "strong"
    int    long_query   = 6;     // >= this many tokens = conceptual/natural-language
    double marginal     = 1.0;   // a long query needs top_score >= this to stay cheap
};

// Deterministic gate: return Deep only when the cheap pass looks insufficient.
inline RetrievalTier classifyRetrievalTier(const QueryTierSignal& s,
                                           const TierThresholds& t = {}) {
    // Nothing matched lexically -> semantic is the only hope.
    if (s.candidate_count <= 0) return RetrievalTier::Deep;

    // "Strong" = the best candidate clears the strong-score floor. (When
    // top_score >= strong_score there is by definition >=1 strong hit, so this
    // stays consistent with strong_hits computed against the same floor, while
    // honoring a caller-tuned threshold.)
    bool hasStrong = s.top_score >= t.strong_score;

    // Long, conceptual queries: a strong lexical hit must ALSO clear the
    // marginal bar, else escalate (embeddings help most here).
    if (s.query_tokens >= t.long_query) {
        if (hasStrong && s.top_score >= t.marginal) return RetrievalTier::Cheap;
        return RetrievalTier::Deep;
    }

    // Short/keyword queries: a strong hit is enough to stay cheap.
    return hasStrong ? RetrievalTier::Cheap : RetrievalTier::Deep;
}

} // namespace icmg::imem
