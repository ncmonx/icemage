// Provenance (Lapis 1): memory_nodes carries a free-text source (default 'unknown').
// MemoryStore ctor guarded-ALTERs the source column onto an existing table (covers
// hand-created fixtures + DBs that skip the migrator). Here we create the base table
// WITHOUT source, then let the ctor add it -- mirrors the real fixture pattern.
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/db.hpp"
#include <string>

using namespace icmg::imem;

// Base schema (NO source column) -- ctor adds source via guarded ALTER.
static void baseSchema(icmg::core::Db& db) {
    db.run("CREATE TABLE IF NOT EXISTS memory_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " topic TEXT NOT NULL, content TEXT NOT NULL, keywords TEXT,"
           " importance INTEGER NOT NULL DEFAULT 1, frequency INTEGER NOT NULL DEFAULT 1,"
           " last_used INTEGER, created_at INTEGER, expires_at INTEGER, deleted_at INTEGER,"
           " zone TEXT NOT NULL DEFAULT 'default', pinned INTEGER NOT NULL DEFAULT 0,"
           " created_by TEXT NOT NULL DEFAULT '', git_sha TEXT NOT NULL DEFAULT '',"
           " row_version INTEGER NOT NULL DEFAULT 0)");
    db.run("CREATE TABLE IF NOT EXISTS memory_keywords("
           " node_id INTEGER, keyword TEXT)");
}

TEST("memory: store with source round-trips (ctor adds column)") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);   // ctor guarded-ALTER adds source
    MemoryNode n;
    n.topic = "decisions-x"; n.content = "use approach B"; n.source = "test-user";
    int64_t id = ms.store(n, /*force=*/true);
    ASSERT_TRUE(id > 0);
    MemoryNode got = ms.get(id);
    ASSERT_EQ(got.source, std::string("test-user"));
}

TEST("memory: store without source defaults to unknown") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    MemoryNode n; n.topic = "t-default"; n.content = "c";  // source left at struct default
    int64_t id = ms.store(n, /*force=*/true);
    MemoryNode got = ms.get(id);
    ASSERT_EQ(got.source, std::string("unknown"));
}
