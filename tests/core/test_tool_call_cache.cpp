// Phase 45 T1: tool-call cache unit tests.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/tool_call_cache.hpp"
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using icmg::core::Db;
using icmg::core::ToolCallCache;

static std::string tmpDb() {
    auto p = fs::temp_directory_path() / "icmg_tcc_test.db";
    std::error_code ec; fs::remove(p, ec);
    return p.string();
}

static void setupSchema(Db& db) {
    db.run("CREATE TABLE IF NOT EXISTS tool_call_cache ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "cmd TEXT NOT NULL,"
           "content_hash TEXT NOT NULL UNIQUE,"
           "result_blob TEXT NOT NULL,"
           "hit_count INTEGER NOT NULL DEFAULT 0,"
           "created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           "expires_at INTEGER NOT NULL)");
}

TEST("cache: makeKey deterministic") {
    auto a = ToolCallCache::makeKey("pack", "task=foo|zone=x");
    auto b = ToolCallCache::makeKey("pack", "task=foo|zone=x");
    auto c = ToolCallCache::makeKey("pack", "task=foo|zone=y");
    ASSERT_EQ(a, b);
    ASSERT_TRUE(a != c);
}

TEST("cache: miss returns nullopt") {
    Db db(tmpDb()); setupSchema(db);
    ToolCallCache tcc(db);
    auto r = tcc.lookup("pack", "task=foo");
    ASSERT_FALSE(r.has_value());
}

TEST("cache: store + lookup hit") {
    Db db(tmpDb()); setupSchema(db);
    ToolCallCache tcc(db);
    tcc.store("pack", "task=foo", "RESULT_BODY", 60);
    auto r = tcc.lookup("pack", "task=foo");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(*r, std::string("RESULT_BODY"));
}

TEST("cache: store same key twice replaces") {
    Db db(tmpDb()); setupSchema(db);
    ToolCallCache tcc(db);
    tcc.store("pack", "task=x", "v1", 60);
    tcc.store("pack", "task=x", "v2", 60);
    auto r = tcc.lookup("pack", "task=x");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(*r, std::string("v2"));
}

TEST("cache: expired entry not returned") {
    Db db(tmpDb()); setupSchema(db);
    ToolCallCache tcc(db);
    tcc.store("pack", "task=ttl", "expires", -10);  // already expired (negative TTL)
    auto r = tcc.lookup("pack", "task=ttl");
    ASSERT_FALSE(r.has_value());
}

TEST("cache: prune removes expired") {
    Db db(tmpDb()); setupSchema(db);
    ToolCallCache tcc(db);
    tcc.store("pack", "old1", "x", -1);
    tcc.store("pack", "old2", "x", -1);
    tcc.store("pack", "fresh", "x", 60);
    int removed = tcc.prune();
    ASSERT_TRUE(removed >= 2);
    ASSERT_TRUE(tcc.lookup("pack", "fresh").has_value());
}

TEST("cache: hit increments hit_count") {
    Db db(tmpDb()); setupSchema(db);
    ToolCallCache tcc(db);
    tcc.store("pack", "task=h", "data", 60);
    tcc.lookup("pack", "task=h");
    tcc.lookup("pack", "task=h");
    auto s = tcc.summary();
    ASSERT_TRUE(s.hits >= 2);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
