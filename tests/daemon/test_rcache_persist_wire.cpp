// v1.78.3 Phase 1+2+3: daemon ctor + protocol scope + lazy hydrate.

#include "../test_main.hpp"
#include "../../src/daemon/rule_daemon.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/recall_cache_persist_db.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using icmg::daemon::RuleDaemon;

static std::string mkTmpDb(const std::string& tag) {
    auto p = fs::temp_directory_path() / ("rcache-wire-" + tag + ".db");
    fs::remove(p);
    return p.string();
}

// pre-seed a temp DB with the persist schema + a couple of rows for scope X.
static std::string mkSeededDb(const std::string& tag, const std::string& scope) {
    auto path = mkTmpDb(tag);
    icmg::core::Db db(path);
    db.run(
        "CREATE TABLE recall_cache_persist ("
        " scope_hash TEXT NOT NULL, key TEXT NOT NULL, value BLOB NOT NULL,"
        " hit_count INTEGER NOT NULL DEFAULT 1, last_used INTEGER NOT NULL,"
        " byte_size INTEGER NOT NULL, PRIMARY KEY (scope_hash, key))");
    icmg::core::writeThrough(db, scope, "warm-k1", "warm-v1", 14);
    icmg::core::writeThrough(db, scope, "warm-k2", "warm-v2", 14);
    return path;
}

// ---- Phase 1 — ctor + dtor lifetime ----------------------------------------

TEST("daemon-wire: ctor opens persist_db_ without throwing") {
    RuleDaemon d(mkTmpDb("ctor"));
    ASSERT_TRUE(true);
}

TEST("daemon-wire: dtor flushes WriteQueue without hang") {
    { RuleDaemon d(mkTmpDb("dtor")); }
    ASSERT_TRUE(true);
}

TEST("daemon-wire: dispatcher still registers RCACHE_* handlers after wire") {
    RuleDaemon d(mkTmpDb("dispatch"));
    auto resp = d.dispatch(R"({"tool":"RCACHE_GET","stdin":"missing-key"})");
    ASSERT_TRUE(resp.find("miss") != std::string::npos);
}

// ---- Phase 2 — protocol scope ext ------------------------------------------

TEST("scope: GET legacy (no scope) miss path returns miss") {
    RuleDaemon d(mkTmpDb("scope1"));
    auto r = d.dispatch(R"({"tool":"RCACHE_GET","stdin":"k1"})");
    ASSERT_TRUE(r.find("miss") != std::string::npos);
}

TEST("scope: PUT + GET round-trip with empty scope (legacy)") {
    RuleDaemon d(mkTmpDb("scope2"));
    d.dispatch(R"({"tool":"RCACHE_PUT","stdin":"{\"key\":\"k1\",\"value\":\"v1\"}"})");
    auto r = d.dispatch(R"({"tool":"RCACHE_GET","stdin":"k1"})");
    ASSERT_TRUE(r.find("v1") != std::string::npos);
}

TEST("scope: PUT scope=A + GET scope=B → miss (per-scope isolation)") {
    RuleDaemon d(mkTmpDb("scope3"));
    d.dispatch(R"({"tool":"RCACHE_PUT","stdin":"{\"key\":\"shared\",\"value\":\"projA-val\",\"scope\":\"A\"}"})");
    auto r = d.dispatch(R"({"tool":"RCACHE_GET","stdin":"shared","scope":"B"})");
    ASSERT_TRUE(r.find("miss") != std::string::npos);
}

TEST("scope: PUT scope=A + GET scope=A → hit (same scope)") {
    RuleDaemon d(mkTmpDb("scope4"));
    d.dispatch(R"({"tool":"RCACHE_PUT","stdin":"{\"key\":\"shared\",\"value\":\"projA-val\",\"scope\":\"A\"}"})");
    auto r = d.dispatch(R"({"tool":"RCACHE_GET","stdin":"shared","scope":"A"})");
    ASSERT_TRUE(r.find("projA-val") != std::string::npos);
}

// ---- Phase 3 — lazy hydrate on first scope touch ---------------------------

TEST("hydrate: pre-seeded scope warm-loads on first GET") {
    auto p = mkSeededDb("hydA", "scopeH1");
    RuleDaemon d(p);
    auto r = d.dispatch(R"({"tool":"RCACHE_GET","stdin":"warm-k1","scope":"scopeH1"})");
    ASSERT_TRUE(r.find("warm-v1") != std::string::npos);
}

TEST("hydrate: pre-seeded scope warm-loads on first PUT (different key)") {
    auto p = mkSeededDb("hydB", "scopeH2");
    RuleDaemon d(p);
    // First touch via PUT a fresh key — hydrate still must fire so warm-k1 is now in RAM.
    d.dispatch(R"({"tool":"RCACHE_PUT","stdin":"{\"key\":\"fresh\",\"value\":\"newval\",\"scope\":\"scopeH2\"}"})");
    auto r = d.dispatch(R"({"tool":"RCACHE_GET","stdin":"warm-k1","scope":"scopeH2"})");
    ASSERT_TRUE(r.find("warm-v1") != std::string::npos);
}

TEST("hydrate: unseeded scope GET still returns miss (no false-positive)") {
    auto p = mkSeededDb("hydC", "scopeH3");
    RuleDaemon d(p);
    auto r = d.dispatch(R"({"tool":"RCACHE_GET","stdin":"warm-k1","scope":"differentScope"})");
    ASSERT_TRUE(r.find("miss") != std::string::npos);
}

TEST("hydrate: empty-scope hydrate path is benign (no crash)") {
    auto p = mkSeededDb("hydD", "");
    RuleDaemon d(p);
    auto r = d.dispatch(R"({"tool":"RCACHE_GET","stdin":"warm-k1"})");
    // empty scope = "" was seeded; should hit.
    ASSERT_TRUE(r.find("warm-v1") != std::string::npos);
}

// ---- Phase 4 — write-through sink ------------------------------------------

// Read row from the persist DB at given path (post-daemon-dtor).
static std::string readPersist(const std::string& path,
                               const std::string& scope,
                               const std::string& key) {
    icmg::core::Db db(path);
    std::string out;
    db.query("SELECT value FROM recall_cache_persist WHERE scope_hash=? AND key=?",
             {scope, key},
             [&](const icmg::core::Row& r) { if (!r.empty()) out = r[0]; });
    return out;
}

static std::string mkEmptyDbWithSchema(const std::string& tag) {
    auto path = mkTmpDb(tag);
    icmg::core::Db db(path);
    db.run(
        "CREATE TABLE recall_cache_persist ("
        " scope_hash TEXT NOT NULL, key TEXT NOT NULL, value BLOB NOT NULL,"
        " hit_count INTEGER NOT NULL DEFAULT 1, last_used INTEGER NOT NULL,"
        " byte_size INTEGER NOT NULL, PRIMARY KEY (scope_hash, key))");
    return path;
}

TEST("sink: PUT writes through to persist DB on dtor-flush") {
    auto p = mkEmptyDbWithSchema("sinkA");
    {
        RuleDaemon d(p);
        d.dispatch(R"({"tool":"RCACHE_PUT","stdin":"{\"key\":\"sk1\",\"value\":\"sv1\",\"scope\":\"S1\"}"})");
    }   // dtor flushes WriteQueue
    auto stored = readPersist(p, "S1", "sk1");
    ASSERT_EQ(stored, std::string("sv1"));
}

TEST("sink: multiple PUTs different scopes persist independently") {
    auto p = mkEmptyDbWithSchema("sinkB");
    {
        RuleDaemon d(p);
        d.dispatch(R"({"tool":"RCACHE_PUT","stdin":"{\"key\":\"k\",\"value\":\"valA\",\"scope\":\"SA\"}"})");
        d.dispatch(R"({"tool":"RCACHE_PUT","stdin":"{\"key\":\"k\",\"value\":\"valB\",\"scope\":\"SB\"}"})");
    }
    ASSERT_EQ(readPersist(p, "SA", "k"), std::string("valA"));
    ASSERT_EQ(readPersist(p, "SB", "k"), std::string("valB"));
}

TEST("sink: ICMG_RECALL_CACHE_PERSIST=0 disables write-through") {
    auto p = mkEmptyDbWithSchema("sinkC");
#ifdef _WIN32
    _putenv_s("ICMG_RECALL_CACHE_PERSIST", "0");
#else
    setenv("ICMG_RECALL_CACHE_PERSIST", "0", 1);
#endif
    {
        RuleDaemon d(p);
        d.dispatch(R"({"tool":"RCACHE_PUT","stdin":"{\"key\":\"k\",\"value\":\"v\",\"scope\":\"S\"}"})");
    }
    auto stored = readPersist(p, "S", "k");
    ASSERT_EQ(stored, std::string(""));  // nothing persisted
#ifdef _WIN32
    _putenv_s("ICMG_RECALL_CACHE_PERSIST", "");
#else
    unsetenv("ICMG_RECALL_CACHE_PERSIST");
#endif
}
