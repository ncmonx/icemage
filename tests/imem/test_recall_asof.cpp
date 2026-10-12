// TDD (2026-08-25): brain v2.22 feature #1 -- time-travel recall (`--as-of T`).
// Research: docs/plans/2026-08-25-brain-memory-v2.22-research.md (Zep/Graphiti
// temporal-KG insight: answering "what did we know AT TIME T" needs point-in-time
// filtering over the bi-temporal columns that ALREADY exist).
//
// allAsOf(T) = nodes with valid_from <= T (0 falls back to created_at) AND
// (invalidated_at == 0 OR invalidated_at > T). Deterministic, pure SQL, no LLM.
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/db.hpp"
#include <string>
#include <algorithm>

using namespace icmg::imem;

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
}

static bool hasId(const std::vector<MemoryNode>& v, int64_t id) {
    return std::any_of(v.begin(), v.end(), [&](const MemoryNode& n){ return n.id == id; });
}

// 1. A live fact stored before T is visible as-of T.
TEST("asof: live fact stored before T is visible") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    MemoryNode n; n.topic = "db"; n.content = "server uses Postgres";
    int64_t id = ms.store(n, true);
    int64_t now = ms.get(id).valid_from;
    auto v = ms.allAsOf(now + 10);
    ASSERT_TRUE(hasId(v, id));
}

// 2. A fact is NOT visible before its valid_from (it wasn't known yet).
TEST("asof: fact not yet valid at T is invisible") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    MemoryNode n; n.topic = "db"; n.content = "server uses Postgres";
    int64_t id = ms.store(n, true);
    int64_t vf = ms.get(id).valid_from;
    auto v = ms.allAsOf(vf - 100);
    ASSERT_TRUE(!hasId(v, id));
}

// 3. An invalidated fact IS visible as-of a time BEFORE invalidation --
//    the whole point of time travel: what did we believe back then?
//    Timestamps are pinned via SQL: nowEpoch() is second-resolution, so on a
//    fast machine store+invalidate land in the SAME second (valid_from ==
//    invalidated_at -> the old fact "lived 0 seconds") and a wall-clock-based
//    assertion is a coin flip. Found 2026-10-12 (failed 5/5 on fast box).
TEST("asof: superseded fact visible before invalidation, hidden after") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    MemoryNode a; a.topic = "db"; a.content = "server uses MySQL";
    int64_t old_id = ms.store(a, true);
    MemoryNode b; b.topic = "db"; b.content = "server uses Postgres now";
    int64_t new_id = ms.store(b, true);
    ASSERT_TRUE(ms.invalidate(old_id, new_id));
    ASSERT_TRUE(ms.get(old_id).invalidated_at > 0);
    // Pin a deterministic timeline: old valid 1000.., new valid 2000..,
    // old invalidated at 3000.
    db.run("UPDATE memory_nodes SET valid_from=1000, invalidated_at=3000 WHERE id=" + std::to_string(old_id));
    db.run("UPDATE memory_nodes SET valid_from=2000 WHERE id=" + std::to_string(new_id));
    // BEFORE invalidation: old fact was the truth.
    auto before = ms.allAsOf(2999);
    ASSERT_TRUE(hasId(before, old_id));
    // AFTER invalidation: old fact hidden (current behavior == all()).
    auto after = ms.allAsOf(3010);
    ASSERT_TRUE(!hasId(after, old_id));
    ASSERT_TRUE(hasId(after, new_id));
}

// 4. Soft-deleted nodes never come back, even in the past view.
TEST("asof: soft-deleted node stays hidden at any T") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    MemoryNode n; n.topic = "tmp"; n.content = "scratch note";
    int64_t id = ms.store(n, true);
    int64_t vf = ms.get(id).valid_from;
    ms.remove(id);
    auto v = ms.allAsOf(vf + 10);
    ASSERT_TRUE(!hasId(v, id));
}

// 5. recallAsOf ranks within the as-of corpus (BM25) -- a superseded fact is
//    retrievable by query at a past T. Timeline pinned via SQL (see test 3).
TEST("asof: recallAsOf finds superseded fact at past T") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    MemoryNode a; a.topic = "db"; a.content = "server uses MySQL for auth";
    int64_t old_id = ms.store(a, true);
    MemoryNode b; b.topic = "db"; b.content = "server uses Postgres for auth";
    int64_t new_id = ms.store(b, true);
    ms.invalidate(old_id, new_id);
    db.run("UPDATE memory_nodes SET valid_from=1000, invalidated_at=3000 WHERE id=" + std::to_string(old_id));
    db.run("UPDATE memory_nodes SET valid_from=2000 WHERE id=" + std::to_string(new_id));
    auto past = ms.recallAsOf("MySQL auth", 2999, 5);
    ASSERT_TRUE(hasId(past, old_id));
    auto now_v = ms.recallAsOf("MySQL auth", 3010, 5);
    ASSERT_TRUE(!hasId(now_v, old_id));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
