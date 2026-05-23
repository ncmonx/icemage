// Tests for `icmg parity` symbol-diff core logic.
// We exercise the GraphStore-level operations (childrenOf + symbol set diff)
// rather than the CLI command directly, since the CLI prints to cout/exit-code.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include <map>
#include <set>
#include <string>

using namespace icmg;

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

// Compute MISSING_IN_NEW + EXTRA_IN_NEW the same way ParityCommand does.
struct ParityResult { std::vector<std::string> missing, extra; };
static ParityResult diffSymbols(core::Db& db, int64_t ref_id, int64_t new_id) {
    graph::GraphStore store(db);
    auto rs = store.childrenOf(ref_id);
    auto ns = store.childrenOf(new_id);
    std::map<std::string, std::string> rmap, nmap;
    for (auto& s : rs) rmap[s.symbol_name] = s.kind;
    for (auto& s : ns) nmap[s.symbol_name] = s.kind;
    ParityResult out;
    for (auto& [n, _] : rmap) if (!nmap.count(n)) out.missing.push_back(n);
    for (auto& [n, _] : nmap) if (!rmap.count(n)) out.extra.push_back(n);
    return out;
}

TEST("parity: identical symbols -> no missing, no extra") {
    auto db = makeDb();
    int64_t a = addFile(db, "ProductMenu.cs");
    int64_t b = addFile(db, "OrderMenu.cs");
    for (auto& n : {"OnLoad", "OnSearchClicked", "Validate"}) {
        addSym(db, a, n, "method");
        addSym(db, b, n, "method");
    }
    auto r = diffSymbols(db, a, b);
    ASSERT_EQ((int)r.missing.size(), 0);
    ASSERT_EQ((int)r.extra.size(), 0);
}

TEST("parity: missing symbol detected") {
    auto db = makeDb();
    int64_t a = addFile(db, "ProductMenu.cs");
    int64_t b = addFile(db, "OrderMenu.cs");
    addSym(db, a, "OnLoad",          "method");
    addSym(db, a, "OnSearchClicked", "method");
    addSym(db, a, "Validate",        "method");
    addSym(db, b, "OnLoad", "method");
    auto r = diffSymbols(db, a, b);
    ASSERT_EQ((int)r.missing.size(), 2);
    ASSERT_EQ((int)r.extra.size(), 0);
}

TEST("parity: extra symbol in new") {
    auto db = makeDb();
    int64_t a = addFile(db, "A.cs");
    int64_t b = addFile(db, "B.cs");
    addSym(db, a, "OnLoad", "method");
    addSym(db, b, "OnLoad",      "method");
    addSym(db, b, "CustomFooter","method");
    auto r = diffSymbols(db, a, b);
    ASSERT_EQ((int)r.missing.size(), 0);
    ASSERT_EQ((int)r.extra.size(),   1);
}

TEST("parity: empty new file -> all ref symbols missing") {
    auto db = makeDb();
    int64_t a = addFile(db, "Ref.cs");
    int64_t b = addFile(db, "New.cs");
    addSym(db, a, "X", "method");
    addSym(db, a, "Y", "property");
    addSym(db, a, "Z", "field");
    auto r = diffSymbols(db, a, b);
    ASSERT_EQ((int)r.missing.size(), 3);
    ASSERT_EQ((int)r.extra.size(),   0);
}

TEST("parity: childrenOf returns only direct children") {
    auto db = makeDb();
    int64_t a = addFile(db, "F.cs");
    int64_t other = addFile(db, "Other.cs");
    addSym(db, a, "MyMethod", "method");
    addSym(db, other, "OtherMethod", "method");
    graph::GraphStore store(db);
    auto kids = store.childrenOf(a);
    ASSERT_EQ((int)kids.size(), 1);
    ASSERT_EQ(kids[0].symbol_name, std::string("MyMethod"));
}

TEST("parity: kind filter (verify on raw set)") {
    auto db = makeDb();
    int64_t a = addFile(db, "A.cs");
    int64_t b = addFile(db, "B.cs");
    addSym(db, a, "Foo", "method");
    addSym(db, a, "Bar", "property");
    addSym(db, b, "Foo", "method");
    // After filtering to method only, no missing.
    graph::GraphStore store(db);
    auto rs = store.childrenOf(a);
    auto ns = store.childrenOf(b);
    std::set<std::string> methods_a, methods_b;
    for (auto& s : rs) if (s.kind == "method") methods_a.insert(s.symbol_name);
    for (auto& s : ns) if (s.kind == "method") methods_b.insert(s.symbol_name);
    int missing = 0;
    for (auto& n : methods_a) if (!methods_b.count(n)) ++missing;
    ASSERT_EQ(missing, 0);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
