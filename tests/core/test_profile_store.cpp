// Zoned profile/skill store CRUD over a temp DB.
// NOTE: mono-test mode shares one process + the temp DB file persists across TESTs and
// reruns. Each TEST uses a DISTINCT user_id so rows never collide (PK upsert keeps reruns
// idempotent).
#include "../test_main.hpp"
#include "../../src/core/profile_store.hpp"
#include "../../src/core/db.hpp"
#include <string>
#include <vector>
using namespace icmg::core;

static std::string tmpDb() { return std::string("profile_store_test.db"); }

TEST("profile_store: put then get round-trips content") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u_get", "work", "lint-rule", "skill", "use clang-tidy with --fix");
    std::string content, kind;
    bool ok = ps.get("u_get", "work", "lint-rule", content, kind);
    ASSERT_TRUE(ok);
    ASSERT_EQ(content, std::string("use clang-tidy with --fix"));
    ASSERT_EQ(kind, std::string("skill"));
}

TEST("profile_store: listZone returns only that zone") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u_list", "work", "a", "note", "x");
    ps.put("u_list", "play", "b", "note", "y");
    auto rows = ps.listZone("u_list", "work");
    ASSERT_EQ(rows.size(), (size_t)1);
    ASSERT_EQ(rows[0].key, std::string("a"));
}

TEST("profile_store: put same key updates (upsert)") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u_upsert", "work", "k", "note", "v1");
    ps.put("u_upsert", "work", "k", "note", "v2");
    std::string c, kind; ps.get("u_upsert", "work", "k", c, kind);
    ASSERT_EQ(c, std::string("v2"));
}

TEST("profile_store: get missing -> false") {
    Db db(tmpDb());
    ProfileStore ps(db);
    std::string c, k;
    ASSERT_TRUE(!ps.get("u_none", "nope", "nope", c, k));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
