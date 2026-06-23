// Gap #5: `icmg memory-history` was double-logged (hook + agent log the same
// recall query) and carried zero metrics. Fix: queryHistoryDetailed() dedups by
// query text (collapsing the doubles, surfacing a count) and query_history grows
// a `tokens` column (estimated recall-result size). The MemoryStore ctor does a
// guarded-ALTER so hand-created fixtures + un-migrated DBs gain the column.
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/core/db.hpp"
#include <string>

using namespace icmg::imem;

// Base query_history WITHOUT tokens column -- ctor adds it via guarded ALTER.
static void baseSchema(icmg::core::Db& db) {
    db.run("CREATE TABLE IF NOT EXISTS query_history("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " query TEXT NOT NULL, matched_ids TEXT,"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
}

TEST("memory-history: ctor guarded-ALTER adds tokens column") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);  // ctor adds tokens column
    bool hasTokens = false;
    db.query("PRAGMA table_info(query_history)", {},
             [&](const icmg::core::Row& r){ if (r.size() > 1 && r[1] == "tokens") hasTokens = true; });
    ASSERT_TRUE(hasTokens);
}

TEST("memory-history: detailed dedups identical queries + counts them") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    // Same query logged 3x (simulates hook + agent + retry double-logging).
    ms.logQuery("gate false positive", 2, 100);
    ms.logQuery("gate false positive", 2, 100);
    ms.logQuery("gate false positive", 2, 100);
    ms.logQuery("other query", 1, 40);

    auto detailed = ms.queryHistoryDetailed(20);
    ASSERT_EQ((int)detailed.size(), 2);   // deduped to 2 distinct queries
    // Find the tripled one.
    int idx = -1;
    for (int i = 0; i < (int)detailed.size(); ++i)
        if (detailed[i].query == "gate false positive") idx = i;
    ASSERT_TRUE(idx >= 0);
    ASSERT_EQ(detailed[idx].count, 3);          // collapsed 3 logs -> count=3
    ASSERT_EQ((long long)detailed[idx].tokens, 300LL); // summed token metric
}

TEST("memory-history: plain queryHistory also dedups") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    ms.logQuery("dup", 1, 10);
    ms.logQuery("dup", 1, 10);
    ms.logQuery("dup", 1, 10);
    auto hist = ms.queryHistory(20);
    ASSERT_EQ((int)hist.size(), 1);   // no repeated "dup" entries
}
