// Phase 24: McpServer resources/list + resources/read tests.
// Drives McpServer's URI dispatcher directly via readResourceUri (friend-of-test
// helper would be cleaner, but exposing via reflection isn't worth it — we drive
// the public path by reproducing the SQL inserts the production code expects).
//
// Strategy: prepare in-memory schema with required tables, inject rows, call
// readResourceUri, assert returned JSON shape.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/mcp/server.hpp"
#include <stdexcept>

using namespace icmg;

// Mirror the schema bits readResourceUri queries.
static core::Db makeMcpDb() {
    core::Db db(":memory:");
    db.run("CREATE TABLE memory_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " topic TEXT, content TEXT, keywords TEXT,"
           " importance INTEGER NOT NULL DEFAULT 1,"
           " frequency INTEGER NOT NULL DEFAULT 0,"
           " last_used INTEGER NOT NULL DEFAULT 0,"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           " expires_at INTEGER, deleted_at INTEGER," " created_by TEXT NOT NULL DEFAULT '', row_version INTEGER NOT NULL DEFAULT 0,"
           " zone TEXT NOT NULL DEFAULT 'default')");
    db.run("CREATE TABLE graph_nodes("
           " id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " path TEXT NOT NULL UNIQUE,"
           " language TEXT, lang TEXT, content TEXT, context TEXT,"
           " kind TEXT NOT NULL DEFAULT 'file',"
           " parent_id INTEGER, symbol_name TEXT,"
           " line_start INTEGER, line_end INTEGER,"
           " zone TEXT NOT NULL DEFAULT 'default')");
    db.run("CREATE TABLE sessions("
           " name TEXT PRIMARY KEY, snapshot TEXT NOT NULL,"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
    return db;
}

TEST("mcp resources: invalid scheme rejected") {
    auto db = makeMcpDb();
    mcp::McpServer s(db);
    bool threw = false;
    try { s.readResourceUri("http://example.com"); }
    catch (const std::exception&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("mcp resources: missing path rejected") {
    auto db = makeMcpDb();
    mcp::McpServer s(db);
    bool threw = false;
    try { s.readResourceUri("icmg://memory"); }
    catch (const std::exception&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("mcp resources: unknown kind rejected") {
    auto db = makeMcpDb();
    mcp::McpServer s(db);
    bool threw = false;
    try { s.readResourceUri("icmg://nonsense/42"); }
    catch (const std::exception&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("mcp resources: memory node lookup") {
    auto db = makeMcpDb();
    db.run("INSERT INTO memory_nodes(id,topic,content,keywords,zone) "
           "VALUES(?,?,?,?,?)",
           {"7", "decisions-test", "Use BM25 freq log(2+f)", "decision,bm25", "core"});
    mcp::McpServer s(db);
    auto j = s.readResourceUri("icmg://memory/7");
    ASSERT_TRUE(j.contains("topic"));
    ASSERT_EQ(j["topic"].get<std::string>(), std::string("decisions-test"));
    ASSERT_EQ(j["zone"].get<std::string>(), std::string("core"));
}

TEST("mcp resources: missing memory id throws") {
    auto db = makeMcpDb();
    mcp::McpServer s(db);
    bool threw = false;
    try { s.readResourceUri("icmg://memory/999"); }
    catch (const std::exception&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("mcp resources: graph node returns expected fields") {
    auto db = makeMcpDb();
    db.run("INSERT INTO graph_nodes(id,path,kind,symbol_name,line_start,line_end,content) "
           "VALUES(?,?,?,?,?,?,?)",
           {"5", "src/foo.cpp", "function", "doStuff", "10", "30", "void doStuff(){}"});
    mcp::McpServer s(db);
    auto j = s.readResourceUri("icmg://graph/5");
    ASSERT_EQ(j["symbol_name"].get<std::string>(), std::string("doStuff"));
    ASSERT_EQ(j["kind"].get<std::string>(), std::string("function"));
}

TEST("mcp resources: session lookup") {
    auto db = makeMcpDb();
    db.run("INSERT INTO sessions(name,snapshot) VALUES(?,?)",
           {"feature-x", "{\"step\":3}"});
    mcp::McpServer s(db);
    auto j = s.readResourceUri("icmg://session/feature-x");
    ASSERT_EQ(j["name"].get<std::string>(), std::string("feature-x"));
    ASSERT_CONTAINS(j["snapshot"].get<std::string>(), "step");
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
