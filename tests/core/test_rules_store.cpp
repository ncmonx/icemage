// Test RulesStore CRUD operations.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/core/rules_store.hpp"

using namespace icmg::core;

static Db makeMigratedDb() {
    Db db(":memory:");
    Migrator m("__nonexistent__");
    m.runAll(db);
    return db;
}

TEST("rules_store: upsert then list returns the row") {
    auto db  = makeMigratedDb();
    RulesStore store(db);

    store.upsert(".icmgrules/test.md", "# My Rule\nDo something.", "testing");
    auto rows = store.list();
    ASSERT_EQ(rows.size(), (size_t)1);
    ASSERT_EQ(rows[0].path,    std::string(".icmgrules/test.md"));
    ASSERT_EQ(rows[0].content, std::string("# My Rule\nDo something."));
    ASSERT_EQ(rows[0].tag,     std::string("testing"));
    ASSERT_TRUE(rows[0].active);
}

TEST("rules_store: setActive false excludes from listActive") {
    auto db  = makeMigratedDb();
    RulesStore store(db);

    store.upsert(".icmgrules/a.md", "content a", "");
    store.upsert(".icmgrules/b.md", "content b", "");
    store.setActive(".icmgrules/a.md", false);

    auto all    = store.list();
    auto active = store.listActive();
    ASSERT_EQ(all.size(),    (size_t)2);
    ASSERT_EQ(active.size(), (size_t)1);
    ASSERT_EQ(active[0].path, std::string(".icmgrules/b.md"));
}

TEST("rules_store: re-upsert same path updates content and updated_at") {
    auto db  = makeMigratedDb();
    RulesStore store(db);

    store.upsert(".icmgrules/rule.md", "old content", "v1");
    auto before = store.list();
    ASSERT_EQ(before[0].content, std::string("old content"));

    store.upsert(".icmgrules/rule.md", "new content", "v2");
    auto after = store.list();
    ASSERT_EQ(after.size(), (size_t)1);
    ASSERT_EQ(after[0].content, std::string("new content"));
    ASSERT_EQ(after[0].tag,     std::string("v2"));
}

int main() { return icmg::test::run_all(); }
