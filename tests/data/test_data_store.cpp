#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/data/data_store.hpp"

using icmg::core::Db;
using icmg::data::DataStore;
using icmg::data::StructuredData;

static Db makeDb() {
    Db db(":memory:");
    db.run(
        "CREATE TABLE structured_data("
        " id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        " data_type  TEXT NOT NULL,"
        " name       TEXT NOT NULL UNIQUE,"
        " scope_path TEXT,"
        " content    TEXT NOT NULL,"
        " version    TEXT NOT NULL DEFAULT '1.0',"
        " tags       TEXT,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );
    db.run(
        "CREATE TABLE data_versions("
        " id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        " data_id     INTEGER NOT NULL REFERENCES structured_data(id) ON DELETE CASCADE,"
        " version     TEXT NOT NULL,"
        " content     TEXT NOT NULL,"
        " changed_by  TEXT,"
        " created_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );
    return db;
}

static StructuredData make(const std::string& type, const std::string& name,
                            const std::string& content, const std::string& scope = "") {
    StructuredData d;
    d.data_type  = type;
    d.name       = name;
    d.content    = content;
    d.scope_path = scope;
    return d;
}

// ---- add / get -------------------------------------------------------------

TEST("data_store: add + get round-trip") {
    auto db = makeDb();
    DataStore store(db);

    auto id = store.add(make("model", "User", "id:int, email:string"));
    ASSERT_TRUE(id > 0);

    auto d = store.get("User");
    ASSERT_TRUE(d.has_value());
    ASSERT_EQ(d->name, std::string("User"));
    ASSERT_EQ(d->data_type, std::string("model"));
    ASSERT_EQ(d->version, std::string("1.0"));
}

TEST("data_store: duplicate name throws") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model", "User", "content"));
    bool threw = false;
    try { store.add(make("model", "User", "other")); }
    catch (const std::runtime_error&) { threw = true; }
    ASSERT_TRUE(threw);
}

// ---- update + versioning ---------------------------------------------------

TEST("data_store: update bumps version") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model", "User", "v1 content"));
    store.update("User", "v2 content", "added field");

    auto d = store.get("User");
    ASSERT_EQ(d->version, std::string("1.1"));
    ASSERT_EQ(d->content, std::string("v2 content"));
}

TEST("data_store: update saves old content to history") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model", "User", "original"));
    store.update("User", "modified");

    auto hist = store.history("User");
    ASSERT_EQ(hist.size(), 1u);
    ASSERT_EQ(hist[0].content, std::string("original"));
    ASSERT_EQ(hist[0].version, std::string("1.0"));
}

TEST("data_store: revert restores old content") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model", "User", "v1"));
    store.update("User", "v2");
    store.update("User", "v3");

    // Revert to 1.0
    bool ok = store.revert("User", "1.0");
    ASSERT_TRUE(ok);

    auto d = store.get("User");
    ASSERT_EQ(d->content, std::string("v1"));
}

// ---- list ------------------------------------------------------------------

TEST("data_store: list all") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model",    "User",      "..."));
    store.add(make("view",     "Dashboard", "..."));
    store.add(make("behavior", "AuthFlow",  "..."));

    auto all = store.list();
    ASSERT_EQ(all.size(), 3u);
}

TEST("data_store: list filter by type") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model", "User",    "..."));
    store.add(make("model", "Invoice", "..."));
    store.add(make("view",  "Dash",    "..."));

    auto models = store.list("model");
    ASSERT_EQ(models.size(), 2u);
}

// ---- search ----------------------------------------------------------------

TEST("data_store: search returns matching entry") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model",    "Invoice", "id:int, amount:decimal, status:enum"));
    store.add(make("behavior", "PayFlow", "create->validate->process"));

    auto res = store.search("invoice amount");
    ASSERT_FALSE(res.empty());
    ASSERT_EQ(res[0].name, std::string("Invoice"));
}

TEST("data_store: search empty query returns all") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model", "A", "x"));
    store.add(make("model", "B", "y"));

    auto res = store.search("");
    ASSERT_EQ(res.size(), 2u);
}

// ---- forFile ---------------------------------------------------------------

TEST("data_store: forFile matches scope prefix") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model", "User",    "...", "src/"));
    store.add(make("model", "Invoice", "...", "src/billing/"));
    store.add(make("model", "Config",  "...", ""));  // global

    auto res = store.forFile("src/billing/invoice_handler.cpp");
    // Should match: User (src/), Invoice (src/billing/), Config (global)
    ASSERT_EQ(res.size(), 3u);

    auto res2 = store.forFile("src/auth/handler.cpp");
    // Should match: User (src/) + Config (global)
    ASSERT_EQ(res2.size(), 2u);
}

TEST("data_store: bumpVersion increments minor") {
    auto db = makeDb();
    DataStore store(db);

    store.add(make("model", "X", "v1"));
    store.update("X", "v2");
    store.update("X", "v3");
    store.update("X", "v4");

    auto d = store.get("X");
    ASSERT_EQ(d->version, std::string("1.3"));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
