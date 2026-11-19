// #285 follow-up: icmg.exe now runs with a UTF-8 ANSI code page (manifest
// activeCodePage). Older builds wrote text through the system code page
// (cp1252 "caf\xE9"), and turning such a row into fs::path now throws 1113.
// repairLegacyAnsiText rewrites those rows to UTF-8 once per DB.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/legacy_text_repair.hpp"
#include <string>

using namespace icmg::core;

namespace {
std::string one(Db& db, const std::string& sql) {
    std::string out;
    db.query(sql, {}, [&](const Row& r) { if (!r.empty()) out = r[0]; });
    return out;
}
const std::string kCp1252Cafe = "caf\xE9";          // legacy ANSI bytes
const std::string kUtf8Cafe   = "caf\xC3\xA9";      // the same text in UTF-8
}  // namespace

TEST("#285b isValidUtf8: accepts ASCII + UTF-8, rejects legacy and malformed bytes") {
    ASSERT_TRUE(isValidUtf8(""));
    ASSERT_TRUE(isValidUtf8("plain/path.txt"));
    ASSERT_TRUE(isValidUtf8(kUtf8Cafe));
    ASSERT_TRUE(isValidUtf8("\xE4\xB8\xAD\xE2\x94\x82\xF0\x9F\x98\x80"));  // CJK, box, emoji
    ASSERT_FALSE(isValidUtf8(kCp1252Cafe));
    ASSERT_FALSE(isValidUtf8("\x97"));                 // cp1252 em-dash
    ASSERT_FALSE(isValidUtf8("\xC0\xAF"));             // overlong '/'
    ASSERT_FALSE(isValidUtf8("\xED\xA0\x80"));         // UTF-16 surrogate
    ASSERT_FALSE(isValidUtf8("\xE4\xB8"));             // truncated
    ASSERT_FALSE(isValidUtf8("\xF4\x90\x80\x80"));     // > U+10FFFF
}

#ifdef _WIN32
TEST("#285b legacyAnsiToUtf8: cp1252 bytes become UTF-8") {
    ASSERT_EQ(legacyAnsiToUtf8(kCp1252Cafe, 1252), kUtf8Cafe);
    ASSERT_EQ(legacyAnsiToUtf8("a\x97" "b", 1252), std::string("a\xE2\x80\x94" "b"));
}

TEST("#285b repairLegacyAnsiText: fixes legacy rows, leaves UTF-8 / ASCII / BLOB alone") {
    Db db(":memory:");
    db.run("CREATE TABLE nodes(id INTEGER PRIMARY KEY, path TEXT, note TEXT, vec BLOB)");
    db.run("INSERT INTO nodes(id, path, note) VALUES(1, ?, 'ascii')", {kCp1252Cafe});
    db.run("INSERT INTO nodes(id, path, note) VALUES(2, ?, ?)", {kUtf8Cafe, "\xE4\xB8\xAD"});
    // A BLOB with the same bytes must never be "repaired".
    db.run("UPDATE nodes SET vec = CAST(? AS BLOB) WHERE id = 1", {kCp1252Cafe});

    ASSERT_EQ(repairLegacyAnsiText(db, 1252), 1);
    ASSERT_EQ(one(db, "SELECT path FROM nodes WHERE id=1"), kUtf8Cafe);
    ASSERT_EQ(one(db, "SELECT note FROM nodes WHERE id=1"), std::string("ascii"));
    ASSERT_EQ(one(db, "SELECT path FROM nodes WHERE id=2"), kUtf8Cafe);
    ASSERT_EQ(one(db, "SELECT note FROM nodes WHERE id=2"), std::string("\xE4\xB8\xAD"));
    ASSERT_EQ(one(db, "SELECT hex(vec) FROM nodes WHERE id=1"), std::string("636166E9"));
}

TEST("#285b repairLegacyAnsiText: runs once per DB (marker)") {
    Db db(":memory:");
    db.run("CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT)");
    ASSERT_EQ(repairLegacyAnsiText(db, 1252), 0);
    // Written after the one-time pass: left alone, the pass does not rescan.
    db.run("INSERT INTO t(id, s) VALUES(1, ?)", {kCp1252Cafe});
    ASSERT_EQ(repairLegacyAnsiText(db, 1252), 0);
    ASSERT_EQ(one(db, "SELECT hex(s) FROM t WHERE id=1"), std::string("636166E9"));
}

TEST("#285b repairLegacyAnsiText: external-content FTS stays in sync via triggers") {
    Db db(":memory:");
    db.run("CREATE TABLE g(id INTEGER PRIMARY KEY, path TEXT)");
    db.run("CREATE VIRTUAL TABLE g_fts USING fts5(path, content='g', content_rowid='id')");
    db.run("CREATE TRIGGER g_ai AFTER INSERT ON g BEGIN "
           "INSERT INTO g_fts(rowid, path) VALUES (new.id, new.path); END");
    db.run("CREATE TRIGGER g_au AFTER UPDATE ON g BEGIN "
           "INSERT INTO g_fts(g_fts, rowid, path) VALUES('delete', old.id, old.path); "
           "INSERT INTO g_fts(rowid, path) VALUES (new.id, new.path); END");
    db.run("INSERT INTO g(id, path) VALUES(1, ?)", {kCp1252Cafe});

    ASSERT_EQ(repairLegacyAnsiText(db, 1252), 1);
    ASSERT_EQ(one(db, "SELECT count(*) FROM g_fts WHERE g_fts MATCH '\"" + kUtf8Cafe + "\"'"),
              std::string("1"));
    db.run("INSERT INTO g_fts(g_fts) VALUES('integrity-check')");
}

TEST("#285b repairLegacyAnsiText: UTF-8 system code page is a no-op") {
    Db db(":memory:");
    db.run("CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT)");
    db.run("INSERT INTO t(id, s) VALUES(1, ?)", {kCp1252Cafe});
    ASSERT_EQ(repairLegacyAnsiText(db, 65001), 0);
    ASSERT_EQ(one(db, "SELECT hex(s) FROM t WHERE id=1"), std::string("636166E9"));
}
#endif

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
