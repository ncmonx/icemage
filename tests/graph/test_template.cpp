// Phase 25 T2/T3: template manifest persistence + apply parity-check core.
// Exercises DB round-trip + child-symbol extraction (the pieces Test
// can drive without spawning the CLI).
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include "../../src/embed/embedder.hpp"   // fnv1a64
#include <nlohmann/json.hpp>
#include <set>

using namespace icmg;
using nlohmann::json;

static core::Db makeDb() {
    core::Db db(":memory:");
    db.run("PRAGMA foreign_keys=ON");
    db.run(
        "CREATE TABLE graph_nodes("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " path TEXT NOT NULL UNIQUE,"
        " lang TEXT, context TEXT, symbols TEXT,"
        " size_bytes INTEGER, file_hash TEXT,"
        " access_count INTEGER NOT NULL DEFAULT 0,"
        " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " group_id TEXT, zone TEXT NOT NULL DEFAULT 'default',"
        " parent_id INTEGER REFERENCES graph_nodes(id) ON DELETE CASCADE,"
        " kind TEXT NOT NULL DEFAULT 'file',"
        " symbol_name TEXT, signature TEXT,"
        " line_start INTEGER, line_end INTEGER, body_hash TEXT)");
    db.run("CREATE TABLE graph_edges("
           " src INTEGER NOT NULL, dst INTEGER NOT NULL,"
           " edge_type TEXT NOT NULL, weight REAL NOT NULL DEFAULT 1.0,"
           " PRIMARY KEY(src,dst,edge_type))");
    db.run("CREATE TABLE templates("
           " name TEXT PRIMARY KEY, source_path TEXT NOT NULL,"
           " manifest_json TEXT NOT NULL, body_hash TEXT NOT NULL DEFAULT '',"
           " memoir_id INTEGER,"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
    return db;
}

static int64_t addFile(core::Db& db, const std::string& path) {
    db.run("INSERT INTO graph_nodes(path, kind) VALUES(?, 'file')", {path});
    int64_t id = 0;
    db.query("SELECT id FROM graph_nodes WHERE path=?", {path},
             [&](const core::Row& r){ id = std::stoll(r[0]); });
    return id;
}
static void addSym(core::Db& db, int64_t parent, const std::string& name, const std::string& kind) {
    db.run("INSERT INTO graph_nodes(path, parent_id, kind, symbol_name) "
           "VALUES(? || ':' || ?, ?, ?, ?)",
           {std::to_string(parent), name, std::to_string(parent), kind, name});
}

TEST("template: round-trip insert/select/delete via SQL") {
    auto db = makeDb();
    db.run("INSERT INTO templates(name, source_path, manifest_json, body_hash) "
           "VALUES(?, ?, ?, ?)",
           {"menu", "Views/ProductMenu.cs", "{\"version\":1}", "abc123"});
    int n = 0;
    db.query("SELECT COUNT(*) FROM templates", {},
             [&](const core::Row& r){ if (!r.empty()) n = std::stoi(r[0]); });
    ASSERT_EQ(n, 1);

    std::string mj;
    db.query("SELECT manifest_json FROM templates WHERE name=?", {"menu"},
             [&](const core::Row& r){ if (!r.empty()) mj = r[0]; });
    ASSERT_CONTAINS(mj, "version");

    db.run("DELETE FROM templates WHERE name=?", {"menu"});
    db.query("SELECT COUNT(*) FROM templates", {},
             [&](const core::Row& r){ if (!r.empty()) n = std::stoi(r[0]); });
    ASSERT_EQ(n, 0);
}

TEST("template: ON CONFLICT DO UPDATE refreshes existing") {
    auto db = makeDb();
    db.run("INSERT INTO templates(name, source_path, manifest_json, body_hash) "
           "VALUES(?, ?, ?, ?)",
           {"x", "old.cs", "{\"v\":1}", "old-hash"});
    db.run("INSERT INTO templates(name, source_path, manifest_json, body_hash) "
           "VALUES(?, ?, ?, ?) ON CONFLICT(name) DO UPDATE SET "
           "source_path=excluded.source_path, manifest_json=excluded.manifest_json, "
           "body_hash=excluded.body_hash",
           {"x", "new.cs", "{\"v\":2}", "new-hash"});
    std::string sp, h;
    db.query("SELECT source_path, body_hash FROM templates WHERE name=?", {"x"},
             [&](const core::Row& r){ sp = r[0]; h = r[1]; });
    ASSERT_EQ(sp, std::string("new.cs"));
    ASSERT_EQ(h,  std::string("new-hash"));
}

TEST("template: apply check identifies missing required_symbols") {
    auto db = makeDb();
    int64_t target = addFile(db, "OrderMenu.cs");
    addSym(db, target, "OnLoad", "method");
    // Manifest requires {OnLoad, OnSearchClicked, Validate}; target only has OnLoad.
    json m;
    m["required_symbols"] = json::array({
        {{"name", "OnLoad"},          {"kind", "method"}},
        {{"name", "OnSearchClicked"}, {"kind", "method"}},
        {{"name", "Validate"},        {"kind", "method"}}
    });
    graph::GraphStore store(db);
    auto kids = store.childrenOf(target);
    std::set<std::string> have;
    for (auto& s : kids) have.insert(s.symbol_name);
    int missing = 0;
    for (auto& rs : m["required_symbols"]) {
        if (!have.count(rs.value("name", ""))) ++missing;
    }
    ASSERT_EQ(missing, 2);
}

TEST("template: body_hash staleness detection via fnv1a64") {
    auto h1 = embed::fnv1a64("class Foo { void X() {} }");
    auto h2 = embed::fnv1a64("class Foo { void X() {} void Y() {} }");
    ASSERT_TRUE(h1 != h2);
    auto h3 = embed::fnv1a64("class Foo { void X() {} }");
    ASSERT_EQ(h1, h3);
}

TEST("template: manifest schema build basic shape") {
    json m;
    m["version"]            = 1;
    m["name"]               = "menu";
    m["source"]             = "Views/ProductMenu.cs";
    m["required_symbols"]   = json::array({
        {{"name", "OnLoad"}, {"kind", "method"}, {"signature", "void OnLoad()"}}
    });
    m["structural_markers"] = json::array({"using System.Windows.Forms"});
    m["kind_counts"]        = {{"method", 1}};
    std::string s = m.dump();
    ASSERT_CONTAINS(s, "required_symbols");
    ASSERT_CONTAINS(s, "OnLoad");
    ASSERT_CONTAINS(s, "structural_markers");
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
