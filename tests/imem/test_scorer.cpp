#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/scorer.hpp"
#include <cmath>

// ---- BM25 Scorer integration tests (in-memory DB) --------------------------
// Tests go through MemoryStore.recall() which internally uses Scorer.

static void setupSchema(icmg::core::Db& db) {
    db.run(
        "CREATE TABLE IF NOT EXISTS memory_nodes("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " topic TEXT NOT NULL,"
        " content TEXT NOT NULL,"
        " keywords TEXT,"
        " importance INTEGER NOT NULL DEFAULT 1,"
        " frequency INTEGER NOT NULL DEFAULT 1,"
        " last_used INTEGER,"
        " expires_at INTEGER,"
        " deleted_at INTEGER,"
        " created_by TEXT NOT NULL DEFAULT ''," 
        " row_version INTEGER NOT NULL DEFAULT 0,"
        
        " zone TEXT NOT NULL DEFAULT 'default',"
        " pinned INTEGER NOT NULL DEFAULT 0,"
        " git_sha TEXT NOT NULL DEFAULT '',"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );
    db.run(
        "CREATE TABLE IF NOT EXISTS memory_keywords("
        " memory_id INTEGER NOT NULL,"
        " keyword TEXT NOT NULL,"
        " PRIMARY KEY(memory_id, keyword)"
        ")"
    );
    db.run(
        "CREATE TABLE IF NOT EXISTS query_history("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " query TEXT NOT NULL,"
        " matched_ids TEXT,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );
}

TEST("scorer: empty db returns no results") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);

    auto results = store.recall("anything", 5);
    ASSERT_TRUE(results.empty());
}

TEST("scorer: exact topic match scores highest") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);

    icmg::imem::MemoryNode a;
    a.topic = "cmake build system"; a.content = "CMake config docs"; a.importance = 1;
    icmg::imem::MemoryNode b;
    b.topic = "python scripting"; b.content = "some python info"; b.importance = 1;

    store.store(a, /*force=*/true);
    store.store(b, /*force=*/true);
    icmg::imem::Scorer::instance().invalidate();

    auto results = store.recall("cmake", 10);
    ASSERT_FALSE(results.empty());
    ASSERT_CONTAINS(results[0].topic, "cmake");
}

TEST("scorer: importance=critical ranks higher") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);

    icmg::imem::MemoryNode normal;
    normal.topic = "auth system"; normal.content = "JWT auth normal"; normal.importance = 1;

    icmg::imem::MemoryNode critical;
    critical.topic = "auth system"; critical.content = "JWT auth critical"; critical.importance = 3;

    store.store(normal, /*force=*/true);
    store.store(critical, /*force=*/true);
    icmg::imem::Scorer::instance().invalidate();

    auto results = store.recall("auth JWT", 10);
    ASSERT_EQ(results.size(), 2u);
    ASSERT_EQ(results[0].importance, 3);  // critical first
}

TEST("scorer: soft-deleted node excluded") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);

    icmg::imem::MemoryNode n;
    n.topic = "secret"; n.content = "deleted content"; n.importance = 1;
    int64_t id = store.store(n, /*force=*/true);

    store.remove(id);  // soft-delete
    icmg::imem::Scorer::instance().invalidate();

    auto results = store.recall("secret", 10);
    ASSERT_TRUE(results.empty());
}


// ---- normalizeMinMax: real-magnitude BM25 normalization for hybrid blend --
// Root cause (2026-07-07): MemoryStore::recallSemantic's hybrid BM25+cosine
// blend used a RANK-POSITION fallback (top=1.0, bottom~0.0 by index) instead
// of the actual BM25 score magnitude -- even though Scorer::rank() already
// populates MemoryNode::bm25_score with the real value. Two candidates with
// near-identical real relevance got maximally different blend weight (1.0 vs
// second-place), and two candidates with wildly different real relevance
// got merely adjacent rank slots. This pure function fixes that: real
// min-max normalization over the actual bm25_score magnitudes.

TEST("normalizeMinMax: empty input returns empty") {
    auto out = icmg::imem::Scorer::normalizeMinMax({});
    ASSERT_TRUE(out.empty());
}

TEST("normalizeMinMax: single value normalizes to 1.0") {
    auto out = icmg::imem::Scorer::normalizeMinMax({7.5});
    ASSERT_EQ(out.size(), 1u);
    ASSERT_TRUE(std::fabs(out[0] - 1.0) < 1e-9);
}

TEST("normalizeMinMax: all-equal values normalize to 1.0 (no div-by-zero)") {
    auto out = icmg::imem::Scorer::normalizeMinMax({3.0, 3.0, 3.0});
    ASSERT_EQ(out.size(), 3u);
    for (double v : out) ASSERT_TRUE(std::fabs(v - 1.0) < 1e-9);
}

TEST("normalizeMinMax: max maps to 1.0, min maps to 0.0") {
    auto out = icmg::imem::Scorer::normalizeMinMax({2.0, 10.0, 6.0});
    ASSERT_EQ(out.size(), 3u);
    ASSERT_TRUE(std::fabs(out[0] - 0.0) < 1e-9);  // 2.0 is min
    ASSERT_TRUE(std::fabs(out[1] - 1.0) < 1e-9);  // 10.0 is max
    ASSERT_TRUE(std::fabs(out[2] - 0.5) < 1e-9);  // 6.0 is midpoint
}

TEST("normalizeMinMax: preserves real magnitude gap, not just rank order") {
    // Two candidates nearly tied in real relevance (99 vs 98) should stay
    // nearly tied after normalization -- NOT get maximally different
    // scores the way rank-position (1.0 vs 0.0) would have produced.
    auto out = icmg::imem::Scorer::normalizeMinMax({99.0, 98.0, 1.0});
    ASSERT_TRUE(out[0] > out[1]);              // order preserved
    ASSERT_TRUE(std::fabs(out[0] - out[1]) < 0.05);  // but nearly tied
    ASSERT_TRUE(out[2] < 0.1);                 // the weak one stays low
}

// ---- entityOverlapScore: entity-linking recall boost (Mem0-style, zero-LLM) --
// icmg already extracts "type:value" entity tokens (url/ip/env/mention) into
// candidate.keywords at CAPTURE time (entity_extract.hpp) but recallSemantic
// never cross-references them against the QUERY's own entities. This closes
// that gap deterministically.

TEST("entityOverlapScore: empty query entities -> 0.0 (no signal)") {
    double s = icmg::imem::Scorer::entityOverlapScore({}, "env:HOME url:https://x.com");
    ASSERT_TRUE(std::fabs(s - 0.0) < 1e-9);
}

TEST("entityOverlapScore: no overlap -> 0.0") {
    double s = icmg::imem::Scorer::entityOverlapScore({"env:HOME"}, "url:https://example.com mention:bob");
    ASSERT_TRUE(std::fabs(s - 0.0) < 1e-9);
}

TEST("entityOverlapScore: full overlap -> 1.0") {
    double s = icmg::imem::Scorer::entityOverlapScore(
        {"env:HOME", "mention:bob"}, "some keywords env:HOME and mention:bob here");
    ASSERT_TRUE(std::fabs(s - 1.0) < 1e-9);
}

TEST("entityOverlapScore: partial overlap -> fraction") {
    double s = icmg::imem::Scorer::entityOverlapScore(
        {"env:HOME", "mention:bob", "ip:1.2.3.4"}, "keywords env:HOME only");
    ASSERT_TRUE(std::fabs(s - (1.0/3.0)) < 1e-9);
}

// ---- levenshteinCapped / fuzzyTokenOverlap: --fuzzy typo-tolerance --------
// MemoryStore::recall()/recallInZone() accepted a `fuzzy` bool but silently
// discarded it (named `bool /*fuzzy*/`); recallUnseen threaded it through
// but it still bottomed out in the same no-op recall(). CLI help text for
// --fuzzy says "Fuzzy search fallback" -- so the promised behavior never
// existed. These are the pure building blocks for the real fix.

TEST("levenshteinCapped: identical strings -> 0") {
    ASSERT_EQ(icmg::imem::Scorer::levenshteinCapped("hello", "hello", 5), 0);
}

TEST("levenshteinCapped: one substitution -> 1") {
    ASSERT_EQ(icmg::imem::Scorer::levenshteinCapped("hello", "hallo", 5), 1);
}

TEST("levenshteinCapped: one insertion -> 1") {
    ASSERT_EQ(icmg::imem::Scorer::levenshteinCapped("recall", "recalls", 5), 1);
}

TEST("levenshteinCapped: beyond cap returns cap+1 (early exit, not exact)") {
    // "kitten" -> "sitting" is edit distance 3; cap=1 should early-exit and
    // report >cap without computing the full DP table.
    int d = icmg::imem::Scorer::levenshteinCapped("kitten", "sitting", 1);
    ASSERT_TRUE(d > 1);
}

TEST("fuzzyTokenOverlap: empty query tokens -> 0.0 (no signal)") {
    double s = icmg::imem::Scorer::fuzzyTokenOverlap({}, {"recall", "semantic"}, 2);
    ASSERT_TRUE(std::fabs(s - 0.0) < 1e-9);
}

TEST("fuzzyTokenOverlap: typo'd token still matches within edit distance") {
    // "recallsematic" (typo, missing 'n') vs corpus token "recallsemantic"
    double s = icmg::imem::Scorer::fuzzyTokenOverlap(
        {"recallsematic"}, {"unrelated", "recallsemantic"}, 2);
    ASSERT_TRUE(std::fabs(s - 1.0) < 1e-9);
}

TEST("fuzzyTokenOverlap: no close match -> 0.0") {
    double s = icmg::imem::Scorer::fuzzyTokenOverlap({"xyzzy"}, {"recall", "semantic"}, 2);
    ASSERT_TRUE(std::fabs(s - 0.0) < 1e-9);
}

TEST("fuzzyTokenOverlap: partial -- one of two query tokens matches") {
    double s = icmg::imem::Scorer::fuzzyTokenOverlap(
        {"recallsematic", "totallyunrelatedword"}, {"recallsemantic"}, 2);
    ASSERT_TRUE(std::fabs(s - 0.5) < 1e-9);
}

TEST("recall: exact BM25 match still wins WITHOUT needing the fuzzy fallback") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);

    icmg::imem::MemoryNode a;
    a.topic = "kubernetes deployment"; a.content = "kubectl apply notes"; a.importance = 1;
    store.store(a, /*force=*/true);
    icmg::imem::Scorer::instance().invalidate();

    // fuzzy=true but query already matches exactly -- fallback must NOT
    // fire (and must not change results) since BM25 already found something.
    auto results = store.recall("kubernetes", 10, /*fuzzy=*/true);
    ASSERT_FALSE(results.empty());
    ASSERT_CONTAINS(results[0].topic, "kubernetes");
}

TEST("recall: typo'd query finds nothing WITHOUT --fuzzy (documents prior gap)") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);

    icmg::imem::MemoryNode a;
    a.topic = "recallsemantic function"; a.content = "hybrid bm25 cosine blend"; a.importance = 1;
    store.store(a, /*force=*/true);
    icmg::imem::Scorer::instance().invalidate();

    auto results = store.recall("recallsematic", 10, /*fuzzy=*/false);
    ASSERT_TRUE(results.empty());
}

TEST("recall: typo'd query FOUND with --fuzzy (the actual fix)") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);

    icmg::imem::MemoryNode a;
    a.topic = "recallsemantic function"; a.content = "hybrid bm25 cosine blend"; a.importance = 1;
    store.store(a, /*force=*/true);
    icmg::imem::Scorer::instance().invalidate();

    auto results = store.recall("recallsematic", 10, /*fuzzy=*/true);
    ASSERT_FALSE(results.empty());
    ASSERT_CONTAINS(results[0].topic, "recallsemantic");
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
