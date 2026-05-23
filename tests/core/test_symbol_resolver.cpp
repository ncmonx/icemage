// Unit tests for core::resolveSymbolMention — SKILL > SYMBOL > FILE priority.
#include "../test_main.hpp"
#include "../../src/core/symbol_resolver.hpp"
#include "../../src/core/db.hpp"
#include <string>

using namespace icmg::core;

// ---- helpers ---------------------------------------------------------------

static Db makeTestDb() {
    return Db(":memory:");
}

// Minimal schema needed by the resolver.
static void applySchema(Db& db) {
    // context_nodes (skills live here, tier='skill')
    db.run(
        "CREATE TABLE IF NOT EXISTS context_nodes ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  node_key    TEXT    NOT NULL UNIQUE,"
        "  title       TEXT    NOT NULL DEFAULT '',"
        "  content     TEXT    NOT NULL DEFAULT '',"
        "  source_file TEXT    NOT NULL DEFAULT '',"
        "  tier        TEXT    NOT NULL DEFAULT 'cold',"
        "  tags        TEXT    NOT NULL DEFAULT '[]',"
        "  active      INTEGER NOT NULL DEFAULT 1,"
        "  created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        "  updated_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );

    // graph_nodes (files and Phase-18 symbols)
    db.run(
        "CREATE TABLE IF NOT EXISTS graph_nodes ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  path        TEXT    NOT NULL UNIQUE,"
        "  lang        TEXT    DEFAULT '',"
        "  context     TEXT    DEFAULT '',"
        "  symbols     TEXT    DEFAULT '',"
        "  size_bytes  INTEGER DEFAULT 0,"
        "  file_hash   TEXT    DEFAULT '',"
        "  access_count INTEGER DEFAULT 0,"
        "  updated_at  INTEGER DEFAULT (strftime('%s','now')),"
        "  parent_id   INTEGER DEFAULT 0,"
        "  kind        TEXT    DEFAULT 'file',"
        "  symbol_name TEXT,"
        "  signature   TEXT    DEFAULT '',"
        "  line_start  INTEGER DEFAULT 0,"
        "  line_end    INTEGER DEFAULT 0,"
        "  body_hash   TEXT    DEFAULT '',"
        "  zone        TEXT    DEFAULT 'default'"
        ")"
    );
}

// ---- tests -----------------------------------------------------------------

TEST("strips leading @: @foo and foo produce same kind") {
    auto db = makeTestDb();
    applySchema(db);

    // Empty DB — both should return UNKNOWN, but identical kind.
    auto r1 = resolveSymbolMention("@foo", db);
    auto r2 = resolveSymbolMention("foo",  db);
    ASSERT_EQ((int)r1.kind, (int)r2.kind);
}

TEST("returns UNKNOWN when nothing matches") {
    auto db = makeTestDb();
    applySchema(db);

    auto r = resolveSymbolMention("@nonexistent_xyz_abc", db);
    ASSERT_EQ((int)r.kind, (int)SymbolKind::UNKNOWN);
    ASSERT_TRUE(r.value.empty());
}

TEST("matches skill node_key exactly") {
    auto db = makeTestDb();
    applySchema(db);

    db.run(
        "INSERT INTO context_nodes (node_key, title, content, tier, active) "
        "VALUES ('skills/auth/login', 'Auth Login Skill', 'Login flow.', 'skill', 1)"
    );

    auto r = resolveSymbolMention("@skills/auth/login", db);
    ASSERT_EQ((int)r.kind, (int)SymbolKind::SKILL);
    ASSERT_EQ(r.value, std::string("skills/auth/login"));
    ASSERT_TRUE(r.score > 0.0);
}

TEST("skill takes priority over file when both match") {
    auto db = makeTestDb();
    applySchema(db);

    // Seed a skill node_key with segment "auth".
    db.run(
        "INSERT INTO context_nodes (node_key, title, content, tier, active) "
        "VALUES ('skills/auth/login', 'Auth Login', 'desc', 'skill', 1)"
    );

    // Seed a file node whose basename stem is also "auth".
    db.run(
        "INSERT INTO graph_nodes (path, kind) VALUES ('src/auth.ts', 'file')"
    );

    // Resolve "@auth" — skill segment match should win over file basename match.
    auto r = resolveSymbolMention("@auth", db);
    ASSERT_EQ((int)r.kind, (int)SymbolKind::SKILL);
}

TEST("matches file by basename when no skill or symbol matches") {
    auto db = makeTestDb();
    applySchema(db);

    db.run(
        "INSERT INTO graph_nodes (path, kind) VALUES ('src/parser.cpp', 'file')"
    );

    auto r = resolveSymbolMention("@parser", db);
    ASSERT_EQ((int)r.kind, (int)SymbolKind::FILE);
    ASSERT_EQ(r.value, std::string("src/parser.cpp"));
    ASSERT_TRUE(r.score > 0.0);
}

TEST("matches symbol by symbol_name") {
    auto db = makeTestDb();
    applySchema(db);

    // Insert a parent file node and a symbol child node.
    db.run(
        "INSERT INTO graph_nodes (path, kind, symbol_name) "
        "VALUES ('src/auth.cpp', 'file', NULL)"
    );
    db.run(
        "INSERT INTO graph_nodes (path, kind, symbol_name) "
        "VALUES ('src/auth.cpp::AuthManager', 'class', 'AuthManager')"
    );

    auto r = resolveSymbolMention("@AuthManager", db);
    ASSERT_EQ((int)r.kind, (int)SymbolKind::SYMBOL);
    ASSERT_EQ(r.value, std::string("AuthManager"));
    ASSERT_TRUE(r.score > 0.0);
}

TEST("returns UNKNOWN for ambiguous file match (multiple files with same stem)") {
    auto db = makeTestDb();
    applySchema(db);

    db.run("INSERT INTO graph_nodes (path, kind) VALUES ('src/util.cpp', 'file')");
    db.run("INSERT INTO graph_nodes (path, kind) VALUES ('tests/util.cpp', 'file')");

    // Two files with stem "util" — resolver should not guess.
    auto r = resolveSymbolMention("@util", db);
    // Must not return FILE (ambiguous); UNKNOWN is expected.
    ASSERT_TRUE(r.kind != SymbolKind::FILE);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
