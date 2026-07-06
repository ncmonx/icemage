// TDD (2026-07-06): causal-fact retrieval layer (feature #1 from
// docs/plans/2026-07-04-feature-research-2026-landscape.md).
//
// Layers typed causal edges (caused_by, enables, blocks, ...) OVER the existing
// BM25 memory recall -- NOT replacing it. A recall hit can pull in causally
// linked facts via a 1-hop graph walk, so a fact that doesn't lexically match
// the query still surfaces when it caused/enabled a fact that does.
// Deterministic, no LLM.
//
// Mirrors the guarded-schema fixture pattern: base table only; the MemoryStore
// ctor creates the memory_causal_edges table.
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/db.hpp"
#include <string>
#include <algorithm>
#include <cstdlib>

using namespace icmg::imem;

// Deterministic unit test: disable ALL daemon interaction so store writes
// (rcFlushOnWrite) and recall() never block on another session's rule-daemon
// pipe (ReadFile can hang if a stale pipe from a concurrent icmg process is
// half-open) and never spawn an external daemon. Local, in-process only.
static void isolateDaemon() {
#ifdef _WIN32
    _putenv_s("ICMG_NO_DAEMON", "1");
    _putenv_s("ICMG_RECALL_CACHE", "0");
#else
    setenv("ICMG_NO_DAEMON", "1", 1);
    setenv("ICMG_RECALL_CACHE", "0", 1);
#endif
}

static void baseSchema(icmg::core::Db& db) {
    db.run("CREATE TABLE IF NOT EXISTS memory_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " topic TEXT NOT NULL, content TEXT NOT NULL, keywords TEXT,"
           " importance INTEGER NOT NULL DEFAULT 1, frequency INTEGER NOT NULL DEFAULT 1,"
           " last_used INTEGER, created_at INTEGER, expires_at INTEGER, deleted_at INTEGER,"
           " zone TEXT NOT NULL DEFAULT 'default', pinned INTEGER NOT NULL DEFAULT 0,"
           " created_by TEXT NOT NULL DEFAULT '', git_sha TEXT NOT NULL DEFAULT '',"
           " source TEXT NOT NULL DEFAULT 'unknown', row_version INTEGER NOT NULL DEFAULT 0)");
    db.run("CREATE TABLE IF NOT EXISTS memory_keywords(memory_id INTEGER, keyword TEXT)");
    // recall() logs each query here -- fixture must provide it or recall throws.
    db.run("CREATE TABLE IF NOT EXISTS query_history("
           " id INTEGER PRIMARY KEY AUTOINCREMENT, query TEXT, matched_ids TEXT,"
           " tokens INTEGER NOT NULL DEFAULT 0, created_at INTEGER)");
}

static bool hasContent(const std::vector<MemoryNode>& v, const std::string& sub) {
    return std::any_of(v.begin(), v.end(), [&](const MemoryNode& n){
        return n.content.find(sub) != std::string::npos; });
}

// 1. linkCausal creates an edge that causalNeighbors surfaces (outgoing).
TEST("causal: linkCausal creates an edge visible via causalNeighbors") {
    isolateDaemon();
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    int64_t a = ms.store({.topic="t", .content="deploy failed"}, true);
    int64_t b = ms.store({.topic="t", .content="disk was full"}, true);
    ASSERT_TRUE(ms.linkCausal(a, b, "caused_by"));

    auto out = ms.causalNeighbors(a, /*outgoing=*/true);
    ASSERT_EQ((int)out.size(), 1);
    ASSERT_EQ(out[0].dst, b);
    ASSERT_EQ(out[0].relation, std::string("caused_by"));
}

// 2. linkCausal is idempotent -- same (src,dst,relation) triple = one edge.
TEST("causal: linkCausal is idempotent for the same triple") {
    isolateDaemon();
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    int64_t a = ms.store({.topic="t", .content="A"}, true);
    int64_t b = ms.store({.topic="t", .content="B"}, true);
    ms.linkCausal(a, b, "caused_by");
    ms.linkCausal(a, b, "caused_by");
    ASSERT_EQ((int)ms.causalNeighbors(a, true).size(), 1);
}

// 3. Direction matters: incoming vs outgoing are distinct views.
TEST("causal: causalNeighbors distinguishes incoming from outgoing") {
    isolateDaemon();
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    int64_t a = ms.store({.topic="t", .content="A"}, true);
    int64_t b = ms.store({.topic="t", .content="B"}, true);
    ms.linkCausal(a, b, "caused_by");
    ASSERT_EQ((int)ms.causalNeighbors(a, /*outgoing=*/true).size(), 1);   // a -> b
    ASSERT_EQ((int)ms.causalNeighbors(a, /*outgoing=*/false).size(), 0);  // nothing -> a
    ASSERT_EQ((int)ms.causalNeighbors(b, /*outgoing=*/false).size(), 1);  // a -> b (incoming to b)
}

// 4. recallCausal surfaces a causally-linked fact that does NOT match the query.
TEST("causal: recallCausal 1-hop expands beyond lexical BM25 matches") {
    isolateDaemon();
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    int64_t sympt = ms.store({.topic="t", .content="checkout latency spiked"}, true);
    int64_t cause = ms.store({.topic="t", .content="redis connection pool exhausted"}, true);
    ms.linkCausal(sympt, cause, "caused_by");

    // Plain recall for "latency" finds the symptom but not the (lexically
    // unrelated) cause.
    auto plain = ms.recall("checkout latency", 10);
    ASSERT_TRUE(hasContent(plain, "latency spiked"));
    ASSERT_TRUE(!hasContent(plain, "redis connection pool"));

    // Causal recall walks the edge and surfaces the cause too.
    auto causal = ms.recallCausal("checkout latency", 10);
    ASSERT_TRUE(hasContent(causal, "latency spiked"));
    ASSERT_TRUE(hasContent(causal, "redis connection pool"));
}

// 5. recallCausal without any edges == plain recall (no spurious expansion).
TEST("causal: recallCausal with no edges equals plain recall") {
    isolateDaemon();
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    ms.store({.topic="t", .content="alpha beta gamma"}, true);
    auto causal = ms.recallCausal("alpha", 10);
    ASSERT_TRUE(hasContent(causal, "alpha beta gamma"));
    ASSERT_EQ((int)causal.size(), 1);
}
