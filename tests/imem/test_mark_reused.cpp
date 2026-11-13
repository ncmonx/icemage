// TDD (2026-11-12): observed-reuse signal (research item 6, after ContextRender
// arXiv 2609.37743: boost what the agent actually used, not what was merely
// returned). `recall --get <ids>` is the explicit "I am using this one" step of
// progressive disclosure, so it bumps frequency + last_used for each id once.
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/db.hpp"
#include <string>
#include <vector>

using namespace icmg::imem;

// Same base fixture as test_bitemporal.cpp: the ctor guarded-ALTERs the rest.
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

static int64_t put(MemoryStore& ms, const std::string& topic, const std::string& content) {
    MemoryNode n; n.topic = topic; n.content = content;
    return ms.store(n, /*force=*/true);
}

TEST("markReused: bumps frequency and last_used of each fetched id") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    int64_t a = put(ms, "decisions-a", "alpha fact");
    int64_t b = put(ms, "decisions-b", "beta fact");
    int64_t c = put(ms, "decisions-c", "gamma fact");
    db.run("UPDATE memory_nodes SET last_used=1000");
    int fa = ms.get(a).frequency, fc = ms.get(c).frequency;

    ms.markReused({a, b});
    ASSERT_EQ(ms.get(a).frequency, fa + 1);
    ASSERT_TRUE(ms.get(a).last_used > 1000);
    ASSERT_TRUE(ms.get(b).last_used > 1000);
    ASSERT_EQ(ms.get(c).frequency, fc);        // not fetched -> untouched
    ASSERT_EQ(ms.get(c).last_used, (int64_t)1000);
}

TEST("markReused: a repeated id in one call counts once") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    int64_t a = put(ms, "decisions-a", "alpha fact");
    int fa = ms.get(a).frequency;
    ms.markReused({a, a, a});
    ASSERT_EQ(ms.get(a).frequency, fa + 1);
}

TEST("markReused: empty list and unknown ids are a no-op") {
    icmg::core::Db db(":memory:"); baseSchema(db);
    MemoryStore ms(db);
    int64_t a = put(ms, "decisions-a", "alpha fact");
    int fa = ms.get(a).frequency;
    ms.markReused({});
    ms.markReused({999999});
    ASSERT_EQ(ms.get(a).frequency, fa);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
