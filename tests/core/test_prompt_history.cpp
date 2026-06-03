// Zoned prompt->response history: exact recall (normalized) + lexical find-similar.
// Distinct user_id per TEST (mono-test shares the temp DB file).
#include "../test_main.hpp"
#include "../../src/core/prompt_history.hpp"
#include "../../src/core/db.hpp"
#include <string>
#include <vector>
using namespace icmg::core;

static std::string phDb() { return std::string("prompt_history_test.db"); }

TEST("prompt_history: recall exact (normalized) returns stored response") {
    Db db(phDb());
    PromptHistory ph(db);
    ph.record("u_ph1", "work", "How do I FIX the Build?", "run cmake --preset");
    std::string resp;
    bool ok = ph.recallExact("u_ph1", "work", "how do i fix the build", resp);
    ASSERT_TRUE(ok);
    ASSERT_EQ(resp, std::string("run cmake --preset"));
}

TEST("prompt_history: findSimilar matches on shared prompt terms") {
    Db db(phDb());
    PromptHistory ph(db);
    ph.record("u_ph2", "work", "linker error LNK1104 on icmg", "kill icmg then rebuild");
    auto hits = ph.findSimilar("u_ph2", "LNK1104 error", 5);
    ASSERT_TRUE(hits.size() >= (size_t)1);
    ASSERT_EQ(hits[0].response, std::string("kill icmg then rebuild"));
}

TEST("prompt_history: record same prompt upserts response") {
    Db db(phDb());
    PromptHistory ph(db);
    ph.record("u_ph3", "work", "deploy steps", "v1");
    ph.record("u_ph3", "work", "deploy steps", "v2");
    std::string r; ph.recallExact("u_ph3", "work", "deploy steps", r);
    ASSERT_EQ(r, std::string("v2"));
}

TEST("prompt_history: findSimilar empty terms -> no hits") {
    Db db(phDb());
    PromptHistory ph(db);
    auto hits = ph.findSimilar("u_ph4", "a b", 5);  // all terms <3 chars
    ASSERT_EQ(hits.size(), (size_t)0);
}

TEST("prompt_history: listZone returns stored prompts (zone-scoped)") {
    Db db(phDb());
    PromptHistory ph(db);
    ph.record("u_lz", "alpha", "alpha prompt one", "ra");
    ph.record("u_lz", "alpha", "alpha prompt two", "rb");
    ph.record("u_lz", "beta",  "beta prompt", "rc");
    auto za = ph.listZone("u_lz", "alpha", 100);
    ASSERT_EQ(za.size(), (size_t)2);
    auto all = ph.listZone("u_lz", "", 100);  // empty zone = all
    ASSERT_TRUE(all.size() >= (size_t)3);
}

TEST("prompt_history: forget removes a stored prompt") {
    Db db(phDb());
    PromptHistory ph(db);
    ph.record("u_fg", "z", "delete me prompt", "r");
    std::string r;
    ASSERT_TRUE(ph.recallExact("u_fg", "z", "delete me prompt", r));
    ph.forget("u_fg", "z", "delete me prompt");
    ASSERT_TRUE(!ph.recallExact("u_fg", "z", "delete me prompt", r));
}

TEST("prompt_history: zoneCounts groups by zone busiest-first") {
    Db db(phDb());
    PromptHistory ph(db);
    ph.record("u_zc", "big",   "p1", "r");
    ph.record("u_zc", "big",   "p2", "r");
    ph.record("u_zc", "small", "p3", "r");
    auto zc = ph.zoneCounts("u_zc");
    ASSERT_TRUE(zc.size() >= (size_t)2);
    ASSERT_EQ(zc[0].first, std::string("big"));   // busiest first
    ASSERT_EQ(zc[0].second, 2);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
