#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/rules/rule_store.hpp"
#include "../../src/rules/rule_resolver.hpp"

using icmg::core::Db;
using icmg::rules::Rule;
using icmg::rules::RuleStore;
using icmg::rules::RuleResolver;
using icmg::rules::RuleConflictError;

static Db makeDb() {
    Db db(":memory:");
    db.run(
        "CREATE TABLE rules("
        " id               INTEGER PRIMARY KEY AUTOINCREMENT,"
        " scope_path       TEXT NOT NULL,"
        " rule_type        TEXT NOT NULL,"
        " name             TEXT NOT NULL,"
        " content          TEXT NOT NULL,"
        " priority         INTEGER NOT NULL DEFAULT 0,"
        " active           INTEGER NOT NULL DEFAULT 1,"
        " created_at       INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " supersedes_id    INTEGER,"
        " trial_mode       INTEGER NOT NULL DEFAULT 0,"
        " trial_prompts    INTEGER NOT NULL DEFAULT 0,"
        " trial_threshold  INTEGER NOT NULL DEFAULT 5,"
        " UNIQUE(scope_path, rule_type, name)"
        ")"
    );
    return db;
}

static Rule makeRule(const std::string& scope, const std::string& type,
                     const std::string& name, const std::string& content,
                     int prio = 0) {
    Rule r;
    r.scope_path = scope;
    r.rule_type  = type;
    r.name       = name;
    r.content    = content;
    r.priority   = prio;
    return r;
}

// ---- RuleStore -------------------------------------------------------------

TEST("rule_store: add + all") {
    auto db = makeDb();
    RuleStore store(db);

    auto id = store.add(makeRule("/", "coding", "snake_case", "use snake_case"));
    ASSERT_TRUE(id > 0);

    auto all = store.all();
    ASSERT_EQ(all.size(), 1u);
    ASSERT_EQ(all[0].name, std::string("snake_case"));
}

TEST("rule_store: scope_path normalized to trailing slash") {
    auto db = makeDb();
    RuleStore store(db);

    Rule r = makeRule("src", "arch", "no-raw-sql", "no raw sql");
    store.add(r);

    auto all = store.all();
    ASSERT_EQ(all[0].scope_path, std::string("src/"));
}

TEST("rule_store: duplicate throws RuleConflictError") {
    auto db = makeDb();
    RuleStore store(db);

    store.add(makeRule("/", "coding", "snake_case", "original"));
    bool threw = false;
    try {
        store.add(makeRule("/", "coding", "snake_case", "duplicate"));
    } catch (const RuleConflictError&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

TEST("rule_store: update overwrites existing") {
    auto db = makeDb();
    RuleStore store(db);

    store.add(makeRule("/", "coding", "snake_case", "original"));
    store.add(makeRule("/", "coding", "snake_case", "updated"), /*update=*/true);

    auto all = store.all();
    ASSERT_EQ(all.size(), 1u);
    ASSERT_EQ(all[0].content, std::string("updated"));
}

TEST("rule_store: setActive disable/enable") {
    auto db = makeDb();
    RuleStore store(db);

    auto id = store.add(makeRule("/", "coding", "r1", "content"));
    store.setActive(id, false);

    auto all = store.all();
    ASSERT_FALSE(all[0].active);

    store.setActive(id, true);
    all = store.all();
    ASSERT_TRUE(all[0].active);
}

TEST("rule_store: remove hard deletes") {
    auto db = makeDb();
    RuleStore store(db);

    auto id = store.add(makeRule("/", "coding", "r1", "content"));
    store.remove(id);

    ASSERT_EQ(store.all().size(), 0u);
}

TEST("rule_store: forPath — root rule applies everywhere") {
    auto db = makeDb();
    RuleStore store(db);

    store.add(makeRule("/", "coding", "snake_case", "use snake_case"));

    auto rules = store.forPath("src/api/handler.cpp");
    ASSERT_EQ(rules.size(), 1u);
}

TEST("rule_store: forPath — scope filter") {
    auto db = makeDb();
    RuleStore store(db);

    store.add(makeRule("/",       "coding", "r1", "root rule"));
    store.add(makeRule("src/",    "arch",   "r2", "src rule"));
    store.add(makeRule("src/api/","arch",   "r3", "api rule"));

    auto rules_api  = store.forPath("src/api/handler.cpp");
    auto rules_core = store.forPath("src/core/db.cpp");

    ASSERT_EQ(rules_api.size(),  3u);  // root + src/ + src/api/
    ASSERT_EQ(rules_core.size(), 2u);  // root + src/
}

TEST("rule_store: inactive rule excluded from forPath") {
    auto db = makeDb();
    RuleStore store(db);

    auto id = store.add(makeRule("/", "coding", "r1", "content"));
    store.setActive(id, false);

    ASSERT_EQ(store.forPath("any/file.cpp").size(), 0u);
}

// ---- RuleResolver ----------------------------------------------------------

TEST("resolver: resolve returns root → specific order") {
    auto db = makeDb();
    RuleStore store(db);

    store.add(makeRule("src/api/", "arch", "r3", "api specific"));
    store.add(makeRule("/",        "coding","r1", "root"));
    store.add(makeRule("src/",     "arch",  "r2", "src"));

    RuleResolver resolver(store);
    auto chain = resolver.resolve("src/api/handler.cpp");

    ASSERT_EQ(chain.size(), 3u);
    // Root (shortest scope) first
    ASSERT_EQ(chain[0].scope_path, std::string("/"));
    ASSERT_EQ(chain[2].scope_path, std::string("src/api/"));
}

TEST("resolver: no conflicts when unique names") {
    auto db = makeDb();
    RuleStore store(db);

    store.add(makeRule("/", "coding", "r1", "root rule"));
    store.add(makeRule("src/", "arch", "r2", "src rule"));

    RuleResolver resolver(store);
    auto confs = resolver.conflicts("src/handler.cpp");
    ASSERT_TRUE(confs.empty());
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
