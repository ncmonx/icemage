// #292 item 1: icmg_pack + icmg_graph_symbol MCP tools, --read-only policy.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/graph_store.hpp"
#include "../../src/mcp/base_mcp_tool.hpp"
#include "../../src/mcp/mcp_toolset.hpp"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;
using namespace icmg;
using namespace icmg::mcp;

namespace {

core::Db symbolDb() {
    core::Db db(":memory:");
    db.run("CREATE TABLE graph_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT NOT NULL UNIQUE,"
           " lang TEXT, context TEXT, symbols TEXT, size_bytes INTEGER,"
           " file_hash TEXT, access_count INTEGER NOT NULL DEFAULT 0,"
           " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           " group_id TEXT, zone TEXT NOT NULL DEFAULT 'default',"
           " parent_id INTEGER REFERENCES graph_nodes(id) ON DELETE CASCADE,"
           " kind TEXT NOT NULL DEFAULT 'file', symbol_name TEXT, signature TEXT,"
           " line_start INTEGER, line_end INTEGER, body_hash TEXT)");
    db.run("INSERT INTO graph_nodes(path,lang,kind) VALUES('src/a.cpp','cpp','file')");
    db.run("INSERT INTO graph_nodes(path,lang,kind,parent_id,symbol_name,signature,line_start,line_end)"
           " VALUES('src/a.cpp#parseWidget','cpp','function',1,'parseWidget',"
           "'int parseWidget(const char*)',10,42)");
    return db;
}

}  // namespace

TEST("mcp_pack_symbol: both tools are registered") {
    auto& reg = core::Registry<BaseMcpTool>::instance();
    ASSERT_TRUE(reg.create("icmg_pack") != nullptr);
    ASSERT_TRUE(reg.create("icmg_graph_symbol") != nullptr);
}

TEST("mcp_pack_symbol: graph_symbol returns file + line range") {
    auto db = symbolDb();
    auto t = core::Registry<BaseMcpTool>::instance().create("icmg_graph_symbol");
    json r = t->call({{"name", "parseWidget"}}, db);
    ASSERT_EQ(r["count"].get<int>(), 1);
    ASSERT_EQ(r["matches"][0]["line_start"].get<int>(), 10);
    ASSERT_EQ(r["matches"][0]["line_end"].get<int>(), 42);
    ASSERT_EQ(r["matches"][0]["symbol"].get<std::string>(), std::string("parseWidget"));
}

TEST("mcp_pack_symbol: graph_symbol requires a name") {
    auto db = symbolDb();
    auto t = core::Registry<BaseMcpTool>::instance().create("icmg_graph_symbol");
    bool threw = false;
    try { t->call(json::object(), db); } catch (const McpError&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("mcp_pack_symbol: pack and graph_symbol stay callable read-only, store does not") {
    ASSERT_TRUE(!isToolCallDenied("icmg_pack", "", /*readonly=*/true));
    ASSERT_TRUE(!isToolCallDenied("icmg_graph_symbol", "", true));
    ASSERT_TRUE(isToolCallDenied("icmg_store", "", true));
}

TEST("mcp_pack_symbol: enableMcpReadOnly flips the env switch") {
    enableMcpReadOnly();
    ASSERT_TRUE(mcpReadOnlyFromEnv());
#ifdef _WIN32
    _putenv_s(kMcpReadOnlyEnv, "");
#else
    unsetenv(kMcpReadOnlyEnv);
#endif
    ASSERT_TRUE(!mcpReadOnlyFromEnv());
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
