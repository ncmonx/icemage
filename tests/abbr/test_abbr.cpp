#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/abbreviation/abbr_store.hpp"

using namespace icmg;

// ---------------------------------------------------------------------------
// Helper: create in-memory DB with abbreviations schema
// ---------------------------------------------------------------------------

static core::Db makeDb() {
    core::Db db(":memory:");
    db.run(
        "CREATE TABLE abbreviations("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " short_form TEXT NOT NULL,"
        " full_form  TEXT NOT NULL,"
        " domain     TEXT,"
        " scope_path TEXT,"
        " frequency  INTEGER NOT NULL DEFAULT 0,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " UNIQUE(short_form, domain)"
        ")"
    );
    db.run("CREATE INDEX idx_abbr_short ON abbreviations(short_form)");
    return db;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST("abbr: learn + get") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    abbreviation::Abbreviation a;
    a.short_form = "bkm";
    a.full_form  = "bukti kas masuk";
    a.domain     = "accounting";

    int64_t id = store.learn(a);
    ASSERT_TRUE(id > 0);

    auto got = store.get("bkm");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->full_form, std::string("bukti kas masuk"));
    ASSERT_EQ(got->domain, std::string("accounting"));
}

TEST("abbr: conflict error with helpful message") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    abbreviation::Abbreviation a{0,"bkm","bukti kas masuk","accounting","",0,0};
    store.learn(a);

    bool threw = false;
    try {
        abbreviation::Abbreviation b{0,"bkm","bank knowledge","accounting","",0,0};
        store.learn(b);
    } catch (const abbreviation::AbbrConflictError& e) {
        threw = true;
        std::string msg(e.what());
        ASSERT_TRUE(msg.find("bukti kas masuk") != std::string::npos);
        ASSERT_TRUE(msg.find("--update") != std::string::npos);
    }
    ASSERT_TRUE(threw);
}

TEST("abbr: update replaces full_form") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    abbreviation::Abbreviation a{0,"bkm","bukti kas masuk","accounting","",0,0};
    store.learn(a);

    abbreviation::Abbreviation b{0,"bkm","bank knowledge management","accounting","",0,0};
    store.learn(b, /*update=*/true);

    auto got = store.get("bkm");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->full_form, std::string("bank knowledge management"));
}

TEST("abbr: expand whole-word substitution") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    abbreviation::Abbreviation a{0,"bkm","bukti kas masuk","","",0,0};
    store.learn(a);
    abbreviation::Abbreviation b{0,"ju","jurnal umum","","",0,0};
    store.learn(b);

    std::string result = store.expand("laporan bkm dan ju bulan ini");
    ASSERT_EQ(result, std::string("laporan bukti kas masuk dan jurnal umum bulan ini"));
}

TEST("abbr: expand no partial-word replacement") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    abbreviation::Abbreviation a{0,"po","purchase order","","",0,0};
    store.learn(a);

    // "pol" should not be expanded (not whole-word match for "po")
    std::string result = store.expand("pol adalah polisi");
    ASSERT_EQ(result, std::string("pol adalah polisi"));
}

TEST("abbr: list all + list by domain") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    abbreviation::Abbreviation a{0,"bkm","bukti kas masuk","accounting","",0,0};
    abbreviation::Abbreviation b{0,"po","purchase order","general","",0,0};
    abbreviation::Abbreviation c{0,"ju","jurnal umum","accounting","",0,0};
    store.learn(a); store.learn(b); store.learn(c);

    auto all = store.list();
    ASSERT_EQ(all.size(), 3u);

    auto acc = store.list("accounting");
    ASSERT_EQ(acc.size(), 2u);
}

TEST("abbr: search by short_form and full_form substring") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    abbreviation::Abbreviation a{0,"bkm","bukti kas masuk","","",0,0};
    abbreviation::Abbreviation b{0,"bkk","bukti kas keluar","","",0,0};
    store.learn(a); store.learn(b);

    auto res = store.search("kas");
    ASSERT_EQ(res.size(), 2u);

    auto res2 = store.search("bkm");
    ASSERT_EQ(res2.size(), 1u);
}

TEST("abbr: remove by short_form") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    abbreviation::Abbreviation a{0,"bkm","bukti kas masuk","","",0,0};
    store.learn(a);

    bool ok = store.remove("bkm");
    ASSERT_TRUE(ok);

    auto got = store.get("bkm");
    ASSERT_TRUE(!got.has_value());
}

TEST("abbr: bumpFrequency increments counter") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    abbreviation::Abbreviation a{0,"bkm","bukti kas masuk","","",0,0};
    store.learn(a);
    store.bumpFrequency("bkm");
    store.bumpFrequency("bkm");

    auto got = store.get("bkm");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->frequency, 2);
}

TEST("abbr: priority — more specific domain wins over 'general'") {
    auto db = makeDb();
    abbreviation::AbbrStore store(db);

    // Same short_form, different domains
    abbreviation::Abbreviation gen{0,"po","purchase order","general","",0,0};
    abbreviation::Abbreviation acc{0,"po","posting order","accounting","",0,0};
    store.learn(gen);
    store.learn(acc);

    // Without cwd, accounting (non-"general") should win
    auto got = store.get("po", "");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->full_form, std::string("posting order"));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
