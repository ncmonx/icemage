// ram-brain Phase B: RecallCache wired into MemoryStore.recall + flush-on-write.
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/core/recall_cache.hpp"
#include "../../src/core/db.hpp"
#include <cstdlib>

using namespace icmg;

namespace {
void schema(core::Db& db) {
    db.run("CREATE TABLE IF NOT EXISTS memory_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT, topic TEXT NOT NULL,"
           " content TEXT NOT NULL, keywords TEXT,"
           " importance INTEGER NOT NULL DEFAULT 1, frequency INTEGER NOT NULL DEFAULT 1,"
           " last_used INTEGER, expires_at INTEGER, deleted_at INTEGER,"
           " created_by TEXT NOT NULL DEFAULT '', row_version INTEGER NOT NULL DEFAULT 0,"
           " zone TEXT NOT NULL DEFAULT 'default', pinned INTEGER NOT NULL DEFAULT 0,"
           " git_sha TEXT NOT NULL DEFAULT '', last_returned_session TEXT,"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
    db.run("CREATE TABLE IF NOT EXISTS memory_keywords("
           " memory_id INTEGER NOT NULL, keyword TEXT NOT NULL,"
           " PRIMARY KEY(memory_id, keyword))");
    db.run("CREATE TABLE IF NOT EXISTS query_history("
           " id INTEGER PRIMARY KEY AUTOINCREMENT, query TEXT NOT NULL,"
           " matched_ids TEXT, created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
}
imem::MemoryNode mk(const std::string& content) {
    imem::MemoryNode n; n.topic = "t"; n.content = content; return n;
}
}

TEST("recall-wiring: second identical recall is a cache hit") {
    core::Db db(":memory:"); schema(db);
    imem::MemoryStore ms(db);
    ms.store(mk("alpha bravo charlie"), true);
    auto& cache = imem::MemoryStore::recallCache(); cache.flush();
    auto before = cache.stats().hits;
    ms.recall("alpha", 5);
    ms.recall("alpha", 5);                       // identical -> hit
    ASSERT_TRUE(cache.stats().hits > before);
}

TEST("recall-wiring: store flushes cache (no stale)") {
    core::Db db(":memory:"); schema(db);
    imem::MemoryStore ms(db);
    ms.store(mk("alpha one apple"), true);
    ms.recall("alpha", 5);                        // cached
    ms.store(mk("alpha two banana"), true);       // must flush
    auto r = ms.recall("alpha", 5);
    int hits = 0; for (auto& n : r) if (n.content.find("two") != std::string::npos) ++hits;
    ASSERT_TRUE(hits >= 1);                        // sees new row, not stale
}

TEST("recall-wiring: ICMG_RECALL_CACHE=0 disables caching") {
#ifdef _WIN32
    _putenv_s("ICMG_RECALL_CACHE", "0");
#else
    setenv("ICMG_RECALL_CACHE", "0", 1);
#endif
    core::Db db(":memory:"); schema(db);
    imem::MemoryStore ms(db);
    ms.store(mk("alpha gamma delta"), true);
    auto& cache = imem::MemoryStore::recallCache(); cache.flush();
    ms.recall("alpha", 5); ms.recall("alpha", 5);
    ASSERT_EQ((int)cache.stats().entries, 0);     // nothing cached
#ifdef _WIN32
    _putenv_s("ICMG_RECALL_CACHE", "");
#else
    unsetenv("ICMG_RECALL_CACHE");
#endif
}
