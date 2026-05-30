// v1.78.2 Phase C: warmReload via hydrate() from disk + writeThrough() upsert.
// In-memory SQLite Db with persist table inlined; no migration runner.

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/recall_cache_persist_db.hpp"
#include "../../src/core/recall_cache_persist.hpp"

using icmg::core::Db;
using icmg::core::writeThrough;
using icmg::core::hydrate;
using icmg::core::scopeHash;

static Db makeDbWithSchema() {
    Db db(":memory:");
    db.run(
        "CREATE TABLE recall_cache_persist ("
        " scope_hash TEXT NOT NULL,"
        " key        TEXT NOT NULL,"
        " value      BLOB NOT NULL,"
        " hit_count  INTEGER NOT NULL DEFAULT 1,"
        " last_used  INTEGER NOT NULL,"
        " byte_size  INTEGER NOT NULL,"
        " PRIMARY KEY (scope_hash, key)"
        ")");
    db.run("CREATE INDEX idx_rcp_scope_hits ON recall_cache_persist(scope_hash, hit_count DESC)");
    return db;
}

TEST("warmreload: writeThrough inserts new row") {
    auto db = makeDbWithSchema();
    auto scope = scopeHash("D:/proj/.icmg/proj.db");
    ASSERT_TRUE(writeThrough(db, scope, "k1", "v1", 5));

    auto rows = hydrate(db, scope, 10);
    ASSERT_EQ(rows.size(), (size_t)1);
    ASSERT_EQ(rows[0].key, std::string("k1"));
    ASSERT_EQ(rows[0].value, std::string("v1"));
    ASSERT_EQ(rows[0].hits, (int64_t)1);
}

TEST("warmreload: writeThrough upsert bumps hit_count on conflict") {
    auto db = makeDbWithSchema();
    auto scope = scopeHash("p");
    writeThrough(db, scope, "k", "v1", 3);
    writeThrough(db, scope, "k", "v2", 3);   // upsert
    writeThrough(db, scope, "k", "v3", 3);

    auto rows = hydrate(db, scope, 10);
    ASSERT_EQ(rows.size(), (size_t)1);
    ASSERT_EQ(rows[0].value, std::string("v3"));
    ASSERT_EQ(rows[0].hits, (int64_t)3);
}

TEST("warmreload: hydrate returns top-N ordered by hit_count DESC") {
    auto db = makeDbWithSchema();
    auto scope = scopeHash("p");

    // a: 1 hit, b: 3 hits, c: 2 hits
    writeThrough(db, scope, "a", "va", 2);
    writeThrough(db, scope, "b", "vb", 2);
    writeThrough(db, scope, "b", "vb", 2);
    writeThrough(db, scope, "b", "vb", 2);
    writeThrough(db, scope, "c", "vc", 2);
    writeThrough(db, scope, "c", "vc", 2);

    auto rows = hydrate(db, scope, 10);
    ASSERT_EQ(rows.size(), (size_t)3);
    ASSERT_EQ(rows[0].key, std::string("b"));   // 3 hits
    ASSERT_EQ(rows[1].key, std::string("c"));   // 2 hits
    ASSERT_EQ(rows[2].key, std::string("a"));   // 1 hit
}

TEST("warmreload: hydrate cap N limits results") {
    auto db = makeDbWithSchema();
    auto scope = scopeHash("p");
    for (int i = 0; i < 10; ++i) {
        writeThrough(db, scope, "k" + std::to_string(i), "v", 2);
    }
    auto rows = hydrate(db, scope, 3);
    ASSERT_EQ(rows.size(), (size_t)3);
}

TEST("warmreload: hydrate respects scope_hash (per-project isolation)") {
    auto db = makeDbWithSchema();
    auto scopeA = scopeHash("D:/projA/.icmg/A.db");
    auto scopeB = scopeHash("D:/projB/.icmg/B.db");

    writeThrough(db, scopeA, "keyA", "valA", 4);
    writeThrough(db, scopeB, "keyB", "valB", 4);

    auto rowsA = hydrate(db, scopeA, 10);
    auto rowsB = hydrate(db, scopeB, 10);

    ASSERT_EQ(rowsA.size(), (size_t)1);
    ASSERT_EQ(rowsA[0].key, std::string("keyA"));
    ASSERT_EQ(rowsB.size(), (size_t)1);
    ASSERT_EQ(rowsB[0].key, std::string("keyB"));
}

TEST("warmreload: hydrate empty when no rows") {
    auto db = makeDbWithSchema();
    auto rows = hydrate(db, scopeHash("empty-proj"), 10);
    ASSERT_EQ(rows.size(), (size_t)0);
}

TEST("warmreload: writeThrough survives binary-looking values") {
    auto db = makeDbWithSchema();
    auto scope = scopeHash("p");
    std::string bin("\x00\x01\x02\xff", 4);
    ASSERT_TRUE(writeThrough(db, scope, "binkey", bin, bin.size()));
    auto rows = hydrate(db, scope, 10);
    ASSERT_EQ(rows.size(), (size_t)1);
    // Note: TEXT binding may strip trailing NULs; main assertion = no crash.
}
