// v1.62 F15: semantic-dedup — exercised through the public store() dedup
// path (findSimilar is private). Uses ICMG_DEDUP_SILENT=1 so a duplicate
// returns the existing id instead of throwing, which is easy to assert.
//   (1) Jaccard near-dup collapses to existing id (default, env off)
//   (2) distinct content gets a NEW id
//   (3) ICMG_SEMANTIC_DEDUP=1 with no embeddings degrades gracefully
//       (no crash; Jaccard dedup still works)

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/imem/memory_store.hpp"
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
           " git_sha TEXT NOT NULL DEFAULT '',"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
    db.run("CREATE TABLE IF NOT EXISTS memory_keywords("
           " memory_id INTEGER NOT NULL, keyword TEXT NOT NULL,"
           " PRIMARY KEY(memory_id, keyword))");
    db.run("CREATE TABLE IF NOT EXISTS query_history("
           " id INTEGER PRIMARY KEY AUTOINCREMENT, query TEXT NOT NULL,"
           " matched_ids TEXT, created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
}
imem::MemoryNode mk(const std::string& topic, const std::string& content) {
    imem::MemoryNode n; n.topic = topic; n.content = content; return n;
}
struct SilentDedup {
    SilentDedup()  {
#ifdef _WIN32
        _putenv_s("ICMG_DEDUP_SILENT", "1");
#else
        setenv("ICMG_DEDUP_SILENT", "1", 1);
#endif
    }
    ~SilentDedup() {
#ifdef _WIN32
        _putenv_s("ICMG_DEDUP_SILENT", "");
#else
        unsetenv("ICMG_DEDUP_SILENT");
#endif
    }
};
}  // namespace

TEST("F15: jaccard near-dup collapses to existing id (env off)") {
    SilentDedup _s;
    core::Db db(":memory:"); schema(db);
    imem::MemoryStore mem(db);
    int64_t id1 = mem.store(mk("build", "fixed the linker error in cmake config"), true);
    int64_t id2 = mem.store(mk("build", "fixed the linker error in cmake config"), false);
    ASSERT_EQ(id1, id2);   // dedup hit -> same id
}

TEST("F15: distinct content gets a new id") {
    SilentDedup _s;
    core::Db db(":memory:"); schema(db);
    imem::MemoryStore mem(db);
    int64_t id1 = mem.store(mk("build", "fixed the linker error in cmake config"), true);
    int64_t id2 = mem.store(mk("ui", "changed the button color to blue today"), false);
    ASSERT_TRUE(id2 != id1);
    ASSERT_TRUE(id2 > 0);
}

TEST("F15: ICMG_SEMANTIC_DEDUP=1 with no embeddings degrades gracefully") {
    SilentDedup _s;
#ifdef _WIN32
    _putenv_s("ICMG_SEMANTIC_DEDUP", "1");
#else
    setenv("ICMG_SEMANTIC_DEDUP", "1", 1);
#endif
    core::Db db(":memory:"); schema(db);
    imem::MemoryStore mem(db);
    int64_t id1 = 0, id2 = 0;
    bool threw = false;
    try {
        id1 = mem.store(mk("build", "fixed the linker error in cmake config"), true);
        id2 = mem.store(mk("build", "fixed the linker error in cmake config"), false);
    } catch (...) { threw = true; }
    ASSERT_FALSE(threw);       // semantic pass must not crash w/o embeddings
    ASSERT_EQ(id1, id2);       // Jaccard dedup still collapses
#ifdef _WIN32
    _putenv_s("ICMG_SEMANTIC_DEDUP", "");
#else
    unsetenv("ICMG_SEMANTIC_DEDUP");
#endif
}
