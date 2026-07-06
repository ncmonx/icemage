// TDD (2026-07-06): two-tier retrieval scheduling (feature #3 from
// docs/plans/2026-07-04-feature-research-2026-landscape.md).
//
// Keep the cheap BM25 pass as the default; escalate to the expensive deep tier
// (ONNX/semantic embed, ~5-6s cold-load) ONLY for queries where the cheap pass
// looks weak. Pure, deterministic, header-only classifier -- no ONNX needed to
// unit-test the gate decision.
#include "../test_main.hpp"
#include "../../src/imem/retrieval_tier.hpp"

using icmg::imem::RetrievalTier;
using icmg::imem::QueryTierSignal;
using icmg::imem::classifyRetrievalTier;

// 1. Confident cheap pass (strong top score, has a strong hit) -> stay Cheap.
TEST("tier: strong BM25 result stays on the cheap tier") {
    QueryTierSignal s;
    s.top_score = 5.0; s.strong_hits = 3; s.query_tokens = 2; s.candidate_count = 10;
    ASSERT_TRUE(classifyRetrievalTier(s) == RetrievalTier::Cheap);
}

// 2. Weak cheap pass (low top score, no strong hits) -> escalate to Deep.
TEST("tier: weak BM25 result escalates to the deep tier") {
    QueryTierSignal s;
    s.top_score = 0.1; s.strong_hits = 0; s.query_tokens = 6; s.candidate_count = 2;
    ASSERT_TRUE(classifyRetrievalTier(s) == RetrievalTier::Deep);
}

// 3. Empty cheap pass (nothing matched lexically) -> Deep (semantic may find it).
TEST("tier: zero candidates escalates to deep") {
    QueryTierSignal s;
    s.top_score = 0.0; s.strong_hits = 0; s.query_tokens = 4; s.candidate_count = 0;
    ASSERT_TRUE(classifyRetrievalTier(s) == RetrievalTier::Deep);
}

// 4. A single short keyword with a decent hit stays Cheap (don't pay ONNX for
//    a plain term lookup even if only one candidate).
TEST("tier: short keyword with a decent hit stays cheap") {
    QueryTierSignal s;
    s.top_score = 2.5; s.strong_hits = 1; s.query_tokens = 1; s.candidate_count = 1;
    ASSERT_TRUE(classifyRetrievalTier(s) == RetrievalTier::Cheap);
}

// 5. Long natural-language question with only a marginal top score -> Deep
//    (conceptual queries benefit from semantic even if a weak lexical hit exists).
TEST("tier: long conceptual query with marginal score escalates") {
    QueryTierSignal s;
    s.top_score = 0.4; s.strong_hits = 0; s.query_tokens = 9; s.candidate_count = 3;
    ASSERT_TRUE(classifyRetrievalTier(s) == RetrievalTier::Deep);
}

// 6. Thresholds are configurable; a stricter strong-score floor flips a
//    borderline case from Cheap to Deep.
TEST("tier: configurable thresholds flip a borderline case") {
    QueryTierSignal s;
    s.top_score = 1.2; s.strong_hits = 1; s.query_tokens = 3; s.candidate_count = 4;
    icmg::imem::TierThresholds def;                    // default
    ASSERT_TRUE(classifyRetrievalTier(s, def) == RetrievalTier::Cheap);
    icmg::imem::TierThresholds strict; strict.strong_score = 2.0;
    ASSERT_TRUE(classifyRetrievalTier(s, strict) == RetrievalTier::Deep);
}
