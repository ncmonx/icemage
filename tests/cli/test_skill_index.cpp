// Unit tests for skill index — parse, store, search.
#include "../test_main.hpp"
#include "../../src/core/context_node_store.hpp"
#include "../../src/core/db.hpp"
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace icmg::core;

static Db makeTestDb() { return Db(":memory:"); }

static void applyMigration(Db& db) {
    db.run(
        "CREATE TABLE IF NOT EXISTS context_nodes ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  node_key TEXT NOT NULL UNIQUE, title TEXT NOT NULL,"
        "  content TEXT NOT NULL, source_file TEXT NOT NULL DEFAULT '',"
        "  tier TEXT NOT NULL DEFAULT 'cold', tags TEXT NOT NULL DEFAULT '[]',"
        "  active INTEGER NOT NULL DEFAULT 1,"
        "  created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        "  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))"
    );
}

static ContextNode makeSkill(const std::string& name, const std::string& desc,
                              const std::string& keywords = "[]") {
    ContextNode n;
    std::string slug = name;
    std::transform(slug.begin(), slug.end(), slug.begin(), ::tolower);
    for (auto& c : slug) if (c == ' ') c = '-';
    n.node_key    = "skill-" + slug;
    n.title       = name;
    n.content     = desc;
    n.source_file = "test-skills/" + slug + ".md";
    n.tier        = "skill";
    n.tags        = keywords;
    n.active      = true;
    return n;
}

TEST("skill_index: upsert and list tier=skill") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeSkill("craftsman:debug",  "Systematic debugging workflow"));
    store.upsert(makeSkill("craftsman:plan",   "Structured planning"));
    store.upsert(makeSkill("craftsman:verify", "Evidence-based verification"));

    // Context nodes with other tiers should not appear in skill list
    ContextNode cold;
    cold.node_key = "arch"; cold.title = "Architecture";
    cold.content = "overview"; cold.tier = "cold"; cold.tags = "[]"; cold.active = true;
    store.upsert(cold);

    auto skills = store.list("skill", true);
    ASSERT_EQ((int)skills.size(), 3);
    for (auto& s : skills) ASSERT_EQ(s.tier, std::string("skill"));
}

TEST("skill_index: BM25 search finds relevant skill") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeSkill("craftsman:debug",
        "Use when debugging. Find root cause systematically. Investigation workflow.",
        "[\"debug\",\"bug\",\"error\",\"investigation\"]"));
    store.upsert(makeSkill("craftsman:plan",
        "Use when planning feature implementation. Structured task breakdown.",
        "[\"plan\",\"design\",\"architect\",\"feature\"]"));
    store.upsert(makeSkill("craftsman:test",
        "Use when writing tests. TDD workflow. Unit integration testing.",
        "[\"test\",\"tdd\",\"unit\",\"coverage\"]"));

    auto debug_results = store.search("debug investigation error", "skill", 3, 0.0);
    ASSERT_TRUE(!debug_results.empty());
    ASSERT_EQ(debug_results[0].title, std::string("craftsman:debug"));

    auto test_results = store.search("write unit test coverage", "skill", 3, 0.0);
    ASSERT_TRUE(!test_results.empty());
    ASSERT_EQ(test_results[0].title, std::string("craftsman:test"));
}

TEST("skill_index: skill does not pollute cold search") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeSkill("craftsman:debug", "debugging error investigation root cause"));

    ContextNode cold;
    cold.node_key = "build"; cold.title = "Build Instructions";
    cold.content  = "cmake configure build"; cold.tier = "cold";
    cold.tags = "[]"; cold.active = true;
    store.upsert(cold);

    // Search cold only — should not include skill
    auto cold_results = store.search("cmake build", "cold", 5, 0.0);
    for (auto& r : cold_results) ASSERT_EQ(r.tier, std::string("cold"));

    // Search skill only — should not include cold
    auto skill_results = store.search("debug", "skill", 5, 0.0);
    for (auto& r : skill_results) ASSERT_EQ(r.tier, std::string("skill"));
}

TEST("skill_index: count returns correct skill count") {
    auto db = makeTestDb();
    applyMigration(db);
    ContextNodeStore store(db);

    store.upsert(makeSkill("s1", "skill one"));
    store.upsert(makeSkill("s2", "skill two"));

    ASSERT_EQ(store.count("skill"), 2);
    ASSERT_EQ(store.count("cold"),  0);
}

int main() { return icmg::test::run_all(); }
