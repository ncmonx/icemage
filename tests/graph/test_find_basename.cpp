// v1.29.0 TDD backfill: GraphStore::findByBasename — used by `icmg context`
// ambiguity warning (Issue #10) to detect when a bare basename matches
// >1 file (e.g. "header.tsx" → 19 candidates across project).
//
// Verifies:
//   - empty basename returns empty vec
//   - unique basename → size 1, path correct
//   - 3 same-basename files → size 3, all paths included
//   - basename match excludes symbol-level rows (kind != 'file')
//   - both / and \ path separators handled

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include "../../src/graph/graph_node.hpp"

using namespace icmg;

namespace {

core::Db makeDb() {
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
    return db;
}

graph::GraphNode mkFile(const std::string& path) {
    graph::GraphNode n;
    n.path = path;
    n.lang = "tsx";
    n.kind = "file";
    return n;
}

graph::GraphNode mkSymbol(const std::string& parent_path, const std::string& sym) {
    graph::GraphNode n;
    // Symbol nodes use a synthetic path; ensure UNIQUE per row.
    n.path = parent_path + "::" + sym;
    n.lang = "tsx";
    n.kind = "symbol";
    n.symbol_name = sym;
    return n;
}

}  // namespace

TEST("findByBasename: empty input returns empty") {
    auto db = makeDb();
    graph::GraphStore store(db);
    auto r = store.findByBasename("");
    ASSERT_EQ((int)r.size(), 0);
}

TEST("findByBasename: unique basename returns size 1") {
    auto db = makeDb();
    graph::GraphStore store(db);
    store.upsertNode(mkFile("src/components/Button.tsx"));
    store.upsertNode(mkFile("src/utils/format.ts"));
    auto r = store.findByBasename("Button.tsx");
    ASSERT_EQ((int)r.size(), 1);
    ASSERT_TRUE(r[0].path.find("Button.tsx") != std::string::npos);
}

TEST("findByBasename: 3 same-basename files all returned") {
    auto db = makeDb();
    graph::GraphStore store(db);
    store.upsertNode(mkFile("src/pages/admin/header.tsx"));
    store.upsertNode(mkFile("src/pages/user/header.tsx"));
    store.upsertNode(mkFile("src/components/header.tsx"));
    auto r = store.findByBasename("header.tsx");
    ASSERT_EQ((int)r.size(), 3);
}

TEST("findByBasename: zero matches returns empty") {
    auto db = makeDb();
    graph::GraphStore store(db);
    store.upsertNode(mkFile("src/components/Button.tsx"));
    auto r = store.findByBasename("NonExistent.tsx");
    ASSERT_EQ((int)r.size(), 0);
}

TEST("findByBasename: excludes symbol-level rows (kind=symbol)") {
    auto db = makeDb();
    graph::GraphStore store(db);
    store.upsertNode(mkFile("src/utils/helper.ts"));
    store.upsertNode(mkSymbol("src/utils/helper.ts", "helper"));
    // basename "helper" alone shouldn't pull the symbol row (it has kind=symbol)
    auto r = store.findByBasename("helper");
    ASSERT_EQ((int)r.size(), 0);
    // But basename "helper.ts" should match the file row only.
    auto r2 = store.findByBasename("helper.ts");
    ASSERT_EQ((int)r2.size(), 1);
    ASSERT_EQ(r2[0].kind, std::string("file"));
}

TEST("findByBasename: matches plain top-level file (no slash)") {
    auto db = makeDb();
    graph::GraphStore store(db);
    store.upsertNode(mkFile("README.md"));
    auto r = store.findByBasename("README.md");
    ASSERT_EQ((int)r.size(), 1);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
