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

TEST("profile_store: zoneCounts groups by zone busiest-first") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u_pzc", "alpha", "k1", "note", "x");
    ps.put("u_pzc", "alpha", "k2", "note", "y");
    ps.put("u_pzc", "beta",  "k3", "note", "z");
    auto zc = ps.zoneCounts("u_pzc");
    ASSERT_TRUE(zc.size() >= (size_t)2);
    ASSERT_EQ(zc[0].first, std::string("alpha"));   // busiest first
    ASSERT_EQ(zc[0].second, 2);
}

TEST("profile_store: searchFts finds content matches") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u_fts1", "infra", "deploy", "note", "deploy the release artifact to the staging server rollout");
    ps.put("u_fts1", "infra", "rollback", "note", "rollback the staging deploy when smoke tests fail");
    ps.put("u_fts1", "work", "lint", "note", "use clang-tidy with fix flag");
    auto hits = ps.searchFts("u_fts1", "staging", 10);
    ASSERT_TRUE(hits.size() >= (size_t)2);   // both staging entries
    for (const auto& h : hits) ASSERT_TRUE(h.content.find("staging") != std::string::npos);
}

TEST("profile_store: searchFts scoped to user") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u_fts_a", "z", "k", "note", "zonklewashere alpha entry");
    ps.put("u_fts_b", "z", "k", "note", "zonklewashere beta entry");
    auto hits = ps.searchFts("u_fts_a", "zonklewashere", 10);
    ASSERT_EQ(hits.size(), (size_t)1);
    ASSERT_EQ(hits[0].content, std::string("zonklewashere alpha entry"));
}

TEST("profile_store: searchFts reflects upsert (stale content gone)") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u_fts_up", "z", "k", "note", "originalwordzz here");
    ps.put("u_fts_up", "z", "k", "note", "replacedwordzz here");
    auto stale = ps.searchFts("u_fts_up", "originalwordzz", 10);
    ASSERT_EQ(stale.size(), (size_t)0);   // old content no longer indexed
    auto fresh = ps.searchFts("u_fts_up", "replacedwordzz", 10);
    ASSERT_EQ(fresh.size(), (size_t)1);
}

TEST("profile_store: searchFts after forget removes from index") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u_fts_fg", "z", "k", "note", "deletemezz token here");
    ps.forget("u_fts_fg", "z", "k");
    auto hits = ps.searchFts("u_fts_fg", "deletemezz", 10);
    ASSERT_EQ(hits.size(), (size_t)0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
