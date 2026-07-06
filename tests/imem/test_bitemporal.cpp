// TDD (2026-07-06): bi-temporal fact invalidation (feature #5 from
// docs/plans/2026-07-04-feature-research-2026-landscape.md, Graphiti pattern).
//
// A stored fact carries valid_from (when it became true) and invalidated_at
// (0 = still live). When a newer fact supersedes an older one, the old node is
// invalidated + linked via superseded_by, but kept for history. Recall (via
// all()) prefers LIVE facts; history is still queryable. Deterministic, no LLM.
//
// Mirrors the guarded-ALTER fixture pattern from test_memory_source.cpp: the
// base table is created WITHOUT the bi-temporal columns and the MemoryStore
// ctor adds them.
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/db.hpp"
#include <string>
#include <algorithm>

using namespace icmg::imem;

// Base schema WITHOUT bi-temporal columns -- ctor guarded-ALTERs them in.
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

// 1. A freshly stored fact is live: invalidated_at == 0, valid_from set.
TEST("bitemporal: store yields a live fact (invalidated_at=0, valid_from set)") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    MemoryNode n; n.topic = "db-choice"; n.content = "server uses Postgres";
    int64_t id = ms.store(n, /*force=*/true);
    ASSERT_TRUE(id > 0);
    MemoryNode got = ms.get(id);
    ASSERT_EQ(got.invalidated_at, (int64_t)0);
    ASSERT_TRUE(got.valid_from > 0);
}

// 2. invalidate() supersedes the old fact but KEEPS it (history preserved).
TEST("bitemporal: invalidate marks old fact + links superseded_by, keeps history") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    MemoryNode a; a.topic = "db-choice"; a.content = "server uses Postgres";
    int64_t oldId = ms.store(a, true);
    MemoryNode b; b.topic = "db-choice"; b.content = "server uses MySQL";
    int64_t newId = ms.store(b, true);

    ASSERT_TRUE(ms.invalidate(oldId, newId));

    MemoryNode got = ms.get(oldId);          // history: still retrievable
    ASSERT_TRUE(got.invalidated_at > 0);
    ASSERT_EQ(got.superseded_by, newId);
    ASSERT_EQ(got.content, std::string("server uses Postgres"));  // not destroyed
}

// 3. all() (the recall chokepoint) returns only LIVE facts.
TEST("bitemporal: all() excludes invalidated facts, keeps the live one") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    int64_t oldId = ms.store({.topic="x", .content="old truth"}, true);
    int64_t newId = ms.store({.topic="x", .content="new truth"}, true);
    ms.invalidate(oldId, newId);

    auto live = ms.all();
    ASSERT_TRUE(hasId(live, newId));
    ASSERT_TRUE(!hasId(live, oldId));
}

// 4. history() includes invalidated facts (still queryable).
TEST("bitemporal: history includes both live and invalidated facts") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    int64_t oldId = ms.store({.topic="x", .content="old truth"}, true);
    int64_t newId = ms.store({.topic="x", .content="new truth"}, true);
    ms.invalidate(oldId, newId);

    auto hist = ms.allIncludingInvalidated();
    ASSERT_TRUE(hasId(hist, oldId));
    ASSERT_TRUE(hasId(hist, newId));
}

// 5. invalidate on a missing id is a safe no-op (returns false).
TEST("bitemporal: invalidate of unknown id is a safe no-op") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    ASSERT_TRUE(!ms.invalidate(999999, 0));
}
