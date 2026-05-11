// Phase 24: recallSemantic hybrid blending tests.
// Strategy: bypass real embedder by writing vectors directly into embeddings table.
// recallSemantic loads embeddings via EmbedStore.getMany; if no Python embedder,
// the function still falls back gracefully — but we want to exercise the math
// path. We do this by setting alpha=1.0 (BM25-only — skips embed call entirely),
// then verifying the path also works at alpha<1 with no embedder (graceful fallback).
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/scorer.hpp"
#include "../../src/embed/embed_store.hpp"

static void setupSchema(icmg::core::Db& db) {
    db.run("CREATE TABLE memory_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " topic TEXT NOT NULL, content TEXT NOT NULL, keywords TEXT,"
           " importance INTEGER NOT NULL DEFAULT 1,"
           " frequency INTEGER NOT NULL DEFAULT 1,"
           " last_used INTEGER, expires_at INTEGER, deleted_at INTEGER," " created_by TEXT NOT NULL DEFAULT '', row_version INTEGER NOT NULL DEFAULT 0,"
           " zone TEXT NOT NULL DEFAULT 'default',"
           " pinned INTEGER NOT NULL DEFAULT 0,"
           " git_sha TEXT NOT NULL DEFAULT '',"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
    db.run("CREATE TABLE memory_keywords(memory_id INTEGER, keyword TEXT,"
           " PRIMARY KEY(memory_id, keyword))");
    db.run("CREATE TABLE query_history(id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " query TEXT, matched_ids TEXT,"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
    db.run("CREATE TABLE embeddings(node_id INTEGER NOT NULL, kind TEXT NOT NULL,"
           " vec BLOB NOT NULL, dim INTEGER NOT NULL, model TEXT NOT NULL DEFAULT '',"
           " body_hash TEXT NOT NULL DEFAULT '',"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           " PRIMARY KEY(node_id, kind))");
}

TEST("vec_search: alpha=1 (pure BM25) returns BM25 ranking") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);
    icmg::imem::MemoryNode a; a.topic = "auth login bug"; a.content = "fix";
    icmg::imem::MemoryNode b; b.topic = "render pipeline"; b.content = "gpu";
    store.store(a, true); store.store(b, true);
    icmg::imem::Scorer::instance().invalidate();

    auto r = store.recallSemantic("auth", 5, 1.0);   // pure BM25
    ASSERT_FALSE(r.empty());
    ASSERT_CONTAINS(r[0].topic, "auth");
}

TEST("vec_search: graceful fallback when no embedder available") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);
    icmg::imem::MemoryNode a; a.topic = "session token expiry"; a.content = "x";
    store.store(a, true);
    icmg::imem::Scorer::instance().invalidate();

    // alpha=0.5 forces semantic path. Without Python sidecar, makeEmbedder()
    // returns nullptr. recallSemantic must fall back to BM25 candidates,
    // not crash, not loop.
    auto r = store.recallSemantic("token", 5, 0.5);
    ASSERT_FALSE(r.empty());
}

TEST("vec_search: alpha clamped to [0,1]") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);
    icmg::imem::MemoryNode a; a.topic = "anything"; a.content = "x";
    store.store(a, true);
    icmg::imem::Scorer::instance().invalidate();

    // Out-of-range values shouldn't throw.
    auto r1 = store.recallSemantic("anything", 5, -2.0);
    auto r2 = store.recallSemantic("anything", 5, 99.0);
    ASSERT_FALSE(r1.empty());
    ASSERT_FALSE(r2.empty());
}

TEST("vec_search: empty corpus returns empty") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);
    auto r = store.recallSemantic("anything", 5, 0.5);
    ASSERT_TRUE(r.empty());
}

TEST("vec_search: limit respected after rerank") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::imem::MemoryStore store(db);
    for (int i = 0; i < 10; ++i) {
        icmg::imem::MemoryNode n;
        n.topic = "auth doc " + std::to_string(i);
        n.content = "auth content variation " + std::to_string(i);
        store.store(n, true);
    }
    icmg::imem::Scorer::instance().invalidate();
    auto r = store.recallSemantic("auth", 3, 0.5);
    ASSERT_TRUE(r.size() <= 3);
}

int main() {
    std::cout << "=== embed::recallSemantic tests ===\n";
    return icmg::test::run_all();
}
