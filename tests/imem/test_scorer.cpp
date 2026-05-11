#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/scorer.hpp"

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

int main() {
    std::cout << "=== BM25 Scorer tests ===\n";
    return icmg::test::run_all();
}
