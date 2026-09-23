// TDD (2026-08-25): brain v2.22 feature #2 -- retrieval-failure ledger
// (`icmg memory-health --gaps`). Research: docs/plans/2026-08-25-brain-memory
// -v2.22-research.md (Mem0 production insight: recall queries that come back
// empty/thin are knowledge-gap signals -- the brain was ASKED and had nothing).
//
// findRecallGaps(rows): pure function over query-history rows (query,
// result_count, last_ts). A gap = query whose result_count <= max_results.
// Repeat asks of the same gap rank higher (the agent keeps needing it).
// Deterministic, no LLM, no IO.
#include "../test_main.hpp"
#include "../../src/imem/recall_gaps.hpp"
#include <string>

using namespace icmg::imem;

static GapQueryRow row(const std::string& q, int results, int64_t ts, int asks = 1) {
    GapQueryRow r; r.query = q; r.result_count = results; r.last_ts = ts; r.asks = asks;
    return r;
}

// 1. Empty-result query is a gap; well-served query is not.
TEST("gaps: zero-result query flagged, served query not") {
    std::vector<GapQueryRow> rows = {
        row("orphan proc daemon fix", 0, 1000),
        row("release workflow", 8, 1000),
    };
    auto gaps = findRecallGaps(rows, /*max_results=*/0, /*max_out=*/10);
    ASSERT_EQ((int)gaps.size(), 1);
    ASSERT_EQ(gaps[0].query, std::string("orphan proc daemon fix"));
}

// 2. max_results threshold: thin results (<=N) also count as gaps.
TEST("gaps: thin-result query flagged under higher threshold") {
    std::vector<GapQueryRow> rows = {
        row("vulkan shader hang", 1, 1000),
        row("release workflow", 8, 1000),
    };
    ASSERT_EQ((int)findRecallGaps(rows, 0, 10).size(), 0);
    auto gaps = findRecallGaps(rows, 1, 10);
    ASSERT_EQ((int)gaps.size(), 1);
    ASSERT_EQ(gaps[0].query, std::string("vulkan shader hang"));
}

// 3. Repeat asks rank first (agent keeps needing what the brain lacks).
TEST("gaps: repeated gap ranks above one-off gap") {
    std::vector<GapQueryRow> rows = {
        row("one-off question", 0, 2000, 1),
        row("recurring blind spot", 0, 1000, 5),
    };
    auto gaps = findRecallGaps(rows, 0, 10);
    ASSERT_EQ((int)gaps.size(), 2);
    ASSERT_EQ(gaps[0].query, std::string("recurring blind spot"));
}

// 4. max_out caps the list (top-N strongest).
TEST("gaps: output capped at max_out") {
    std::vector<GapQueryRow> rows;
    for (int i = 0; i < 30; ++i)
        rows.push_back(row("gap query " + std::to_string(i), 0, 1000 + i));
    auto gaps = findRecallGaps(rows, 0, 5);
    ASSERT_EQ((int)gaps.size(), 5);
}

// 5. Trivial/noise queries are skipped (too short to be a real knowledge ask).
TEST("gaps: short noise queries skipped") {
    std::vector<GapQueryRow> rows = {
        row("a", 0, 1000),
        row("ok", 0, 1000),
        row("real question about daemon lifecycle", 0, 900),
    };
    auto gaps = findRecallGaps(rows, 0, 10);
    ASSERT_EQ((int)gaps.size(), 1);
    ASSERT_EQ(gaps[0].query, std::string("real question about daemon lifecycle"));
}

// --- IDE-D (2026-09-23): evicted vs missed ---------------------------------
// Research: "What Eviction Destroys" (arXiv 2609.08279) -- an empty recall has
// two very different causes: the brain NEVER held it (missed -> store it) or it
// WAS held and soft-deleted (evicted -> restore it). Same symptom, opposite
// remedies, so the ledger should say which one it is.

// 6. Default classification is "missed" (no deleted corpus consulted).
TEST("gaps: default kind is missed") {
    std::vector<GapQueryRow> rows = { row("daemon lifecycle teardown", 0, 1000) };
    auto gaps = findRecallGaps(rows, 0, 10);
    ASSERT_EQ((int)gaps.size(), 1);
    ASSERT_EQ((int)gaps[0].kind, (int)GapKind::Missed);
}

// 7. A soft-deleted memory matching the query tokens -> evicted + its id.
TEST("gaps: query matching deleted memory classified evicted") {
    std::vector<GapQueryRow> rows = {
        row("vulkan shader hang glslc", 0, 1000),
        row("kubernetes ingress routing", 0, 900),
    };
    std::vector<DeletedMemRow> deleted = {
        {42, "gotcha: vulkan shader hang caused by glslc zombie process"},
    };
    auto gaps = findRecallGaps(rows, 0, 10, &deleted);
    ASSERT_EQ((int)gaps.size(), 2);
    // ranked by asks/ts equally -> order by input ts desc; find each
    int evicted = -1, missed = -1;
    for (int i = 0; i < 2; ++i)
        (gaps[i].kind == GapKind::Evicted ? evicted : missed) = i;
    ASSERT_TRUE(evicted >= 0 && missed >= 0);
    ASSERT_EQ(gaps[evicted].query, std::string("vulkan shader hang glslc"));
    ASSERT_EQ(gaps[evicted].evicted_id, (int64_t)42);
    ASSERT_EQ(gaps[missed].query, std::string("kubernetes ingress routing"));
    ASSERT_EQ(gaps[missed].evicted_id, (int64_t)0);
}

// 8. One shared trivial word is NOT eviction evidence (needs >=2 real tokens).
TEST("gaps: single weak overlap stays missed") {
    std::vector<GapQueryRow> rows = { row("release notes encoding fix", 0, 1000) };
    std::vector<DeletedMemRow> deleted = {
        {7, "the fix for the daemon spam problem"},   // shares only "fix"
    };
    auto gaps = findRecallGaps(rows, 0, 10, &deleted);
    ASSERT_EQ((int)gaps.size(), 1);
    ASSERT_EQ((int)gaps[0].kind, (int)GapKind::Missed);
}

// 9. Token match is case-insensitive; null deleted list = legacy behaviour.
TEST("gaps: case-insensitive match and null corpus compat") {
    std::vector<GapQueryRow> rows = { row("Vulkan Shader HANG", 0, 1000) };
    std::vector<DeletedMemRow> deleted = {
        {9, "VULKAN shader hang zombie"},
    };
    auto gaps = findRecallGaps(rows, 0, 10, &deleted);
    ASSERT_EQ((int)gaps[0].kind, (int)GapKind::Evicted);
    auto legacy = findRecallGaps(rows, 0, 10, nullptr);
    ASSERT_EQ((int)legacy[0].kind, (int)GapKind::Missed);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
