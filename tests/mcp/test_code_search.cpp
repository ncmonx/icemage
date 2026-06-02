// v2.0.0 externals TDD: icmg_code_search MCP tool — graph-backed code search so
// agents locate symbols/files by query instead of grep/Read.

#include "../test_main.hpp"
#include "../../src/mcp/base_mcp_tool.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include "../../src/graph/graph_node.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace icmg;

namespace {

core::Db makeGraphDb() {
    core::Db db(":memory:");
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

graph::GraphNode sym(const std::string& path, const std::string& name,
                     const std::string& kind, int line) {
    graph::GraphNode n;
    n.path = path; n.symbol_name = name; n.kind = kind;
    n.line_start = line; n.signature = kind + " " + name + "(...)";
    return n;
}

json callTool(const std::string& tool, const json& args, core::Db& db) {
    auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
    if (!reg.has(tool)) throw std::runtime_error("tool not found: " + tool);
    return reg.create(tool)->call(args, db);
}

}  // namespace

TEST("code_search: registered in MCP registry") {
    ASSERT_TRUE(core::Registry<mcp::BaseMcpTool>::instance().has("icmg_code_search"));
}

TEST("code_search: finds symbol by name") {
    auto db = makeGraphDb();
    graph::GraphStore store(db);
    store.upsertNode(sym("src/graph/graph_store.cpp", "resolveAndInsertEdges", "function", 120));
    store.upsertNode(sym("src/core/db.cpp", "openDatabase", "function", 12));

    auto r = callTool("icmg_code_search", {{"query", "resolveAndInsertEdges"}}, db);
    ASSERT_TRUE(r["count"].get<int>() >= 1);
    ASSERT_CONTAINS(r.dump(), std::string("resolveAndInsertEdges"));
    ASSERT_CONTAINS(r.dump(), std::string("graph_store.cpp"));
}

TEST("code_search: kind filter excludes non-matching") {
    auto db = makeGraphDb();
    graph::GraphStore store(db);
    // Shared token "Widget" so a single query matches both kinds; filter must
    // keep only the class. Distinct paths (graph_nodes has UNIQUE(path)).
    store.upsertNode(sym("widget.cpp", "Widget", "class", 5));
    store.upsertNode(sym("render.cpp", "renderWidget", "function", 20));

    auto r = callTool("icmg_code_search", {{"query","Widget"},{"kind","class"}}, db);
    auto dump = r.dump();
    ASSERT_CONTAINS(dump, std::string("\"symbol\":\"Widget\""));
    ASSERT_NOT_CONTAINS(dump, std::string("renderWidget"));
}

TEST("code_search: limit respected") {
    auto db = makeGraphDb();
    graph::GraphStore store(db);
    for (int i = 0; i < 8; ++i)
        store.upsertNode(sym("f" + std::to_string(i) + ".cpp", "fnFoo" + std::to_string(i), "function", i + 1));
    auto r = callTool("icmg_code_search", {{"query","fnFoo"},{"limit",3}}, db);
    ASSERT_TRUE(r["count"].get<int>() <= 3);
}

TEST("code_search: empty query rejected") {
    auto db = makeGraphDb();
    bool threw = false;
    try { (void)callTool("icmg_code_search", json::object(), db); }
    catch (...) { threw = true; }
    ASSERT_TRUE(threw);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
