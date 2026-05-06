#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/icm/memory_store.hpp"
#include "../../src/icm/scorer.hpp"

// ---- BM25 Scorer integration tests (in-memory DB) --------------------------

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
}

TEST("scorer: empty db returns no results") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::icm::MemoryStore store(db);
    icmg::icm::Scorer scorer;

    auto results = scorer.query(db, "anything", 5);
    ASSERT_TRUE(results.empty());
}

TEST("scorer: exact topic match scores highest") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::icm::MemoryStore store(db);
    icmg::icm::Scorer scorer;

    icmg::icm::MemoryNode a;
    a.topic = "cmake build system"; a.content = "CMake config docs"; a.importance = 1;
    icmg::icm::MemoryNode b;
    b.topic = "python scripting"; b.content = "some python info"; b.importance = 1;

    store.store(a);
    store.store(b);
    scorer.invalidate();

    auto results = scorer.query(db, "cmake", 10);
    ASSERT_FALSE(results.empty());
    ASSERT_CONTAINS(results[0].topic, "cmake");
}

TEST("scorer: importance=critical ranks higher") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::icm::MemoryStore store(db);
    icmg::icm::Scorer scorer;

    icmg::icm::MemoryNode normal;
    normal.topic = "auth system"; normal.content = "JWT auth normal"; normal.importance = 1;

    icmg::icm::MemoryNode critical;
    critical.topic = "auth system"; critical.content = "JWT auth critical"; critical.importance = 3;

    store.store(normal);
    store.store(critical);
    scorer.invalidate();

    auto results = scorer.query(db, "auth JWT", 10);
    ASSERT_EQ(results.size(), 2u);
    ASSERT_EQ(results[0].importance, 3);  // critical first
}

TEST("scorer: soft-deleted node excluded") {
    icmg::core::Db db(":memory:");
    setupSchema(db);
    icmg::icm::MemoryStore store(db);
    icmg::icm::Scorer scorer;

    icmg::icm::MemoryNode n;
    n.topic = "secret"; n.content = "deleted content"; n.importance = 1;
    int64_t id = store.store(n);

    store.forget(id);
    scorer.invalidate();

    auto results = scorer.query(db, "secret", 10);
    ASSERT_TRUE(results.empty());
}

int main() {
    std::cout << "=== BM25 Scorer tests ===\n";
    return icmg::test::run_all();
}
