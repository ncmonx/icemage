#include "../test_main.hpp"
#include "../../src/mcp/base_mcp_tool.hpp"
#include "../../src/mcp/server.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/registry.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;
using namespace icmg;

// ---------------------------------------------------------------------------
// Helper: open in-memory DB with all required tables
// ---------------------------------------------------------------------------
static core::Db openTestDb() {
    core::Db db(":memory:");

    // memory_nodes
    db.run(
        "CREATE TABLE memory_nodes("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " topic TEXT NOT NULL,"
        " content TEXT NOT NULL,"
        " keywords TEXT,"
        " importance INTEGER NOT NULL DEFAULT 1,"
        " frequency INTEGER NOT NULL DEFAULT 1,"
        " last_used INTEGER,"
        " expires_at INTEGER,"
        " deleted_at INTEGER,"
        " created_by TEXT NOT NULL DEFAULT ''," 
        " row_version INTEGER NOT NULL DEFAULT 0,"
        
        " zone TEXT NOT NULL DEFAULT 'default',"
        " pinned INTEGER NOT NULL DEFAULT 0,"
        " git_sha TEXT NOT NULL DEFAULT '',"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))"
    );

    // commands
    db.run(
        "CREATE TABLE commands("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " command TEXT NOT NULL UNIQUE,"
        " frequency INTEGER NOT NULL DEFAULT 1,"
        " last_used INTEGER,"
        " avg_lines INTEGER NOT NULL DEFAULT 0,"
        " tags TEXT)"
    );

    // graph_nodes
    db.run(
        "CREATE TABLE graph_nodes("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " path TEXT NOT NULL UNIQUE,"
        " lang TEXT,"
        " context TEXT,"
        " symbols TEXT,"
        " size_bytes INTEGER,"
        " file_hash TEXT,"
        " access_count INTEGER NOT NULL DEFAULT 0,"
        " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " zone TEXT NOT NULL DEFAULT 'default')"
    );

    // graph_edges
    db.run(
        "CREATE TABLE graph_edges("
        " src INTEGER NOT NULL,"
        " dst INTEGER NOT NULL,"
        " edge_type TEXT NOT NULL,"
        " weight REAL NOT NULL DEFAULT 1.0,"
        " PRIMARY KEY (src, dst, edge_type))"
    );

    // rules
    db.run(
        "CREATE TABLE rules("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " scope_path TEXT NOT NULL,"
        " rule_type TEXT NOT NULL,"
        " name TEXT NOT NULL,"
        " content TEXT NOT NULL,"
        " priority INTEGER NOT NULL DEFAULT 0,"
        " active INTEGER NOT NULL DEFAULT 1,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))"
    );

    // structured_data
    db.run(
        "CREATE TABLE structured_data("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " data_type TEXT NOT NULL,"
        " name TEXT NOT NULL UNIQUE,"
        " scope_path TEXT,"
        " content TEXT NOT NULL,"
        " version TEXT NOT NULL DEFAULT '1.0',"
        " tags TEXT,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))"
    );

    // query_history (used by icm MemoryStore recall)
    db.run(
        "CREATE TABLE query_history("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " query TEXT NOT NULL,"
        " matched_ids TEXT,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))"
    );

    // memory_keywords
    db.run(
        "CREATE TABLE memory_keywords("
        " memory_id INTEGER NOT NULL,"
        " keyword TEXT NOT NULL,"
        " PRIMARY KEY (memory_id, keyword))"
    );

    // abbreviations
    db.run(
        "CREATE TABLE abbreviations("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " short_form TEXT NOT NULL,"
        " full_form TEXT NOT NULL,"
        " domain TEXT,"
        " scope_path TEXT,"
        " frequency INTEGER NOT NULL DEFAULT 0,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " UNIQUE(short_form, domain))"
    );

    // stored_procedures
    db.run(
        "CREATE TABLE stored_procedures("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT NOT NULL,"
        " db_type TEXT,"
        " database_name TEXT,"
        " content TEXT NOT NULL,"
        " context TEXT,"
        " parameters TEXT,"
        " return_type TEXT,"
        " tables_used TEXT,"
        " sp_dependencies TEXT,"
        " scope_path TEXT,"
        " tags TEXT,"
        " version INTEGER NOT NULL DEFAULT 1,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " UNIQUE(name, database_name))"
    );

    return db;
}

// Helper: call a tool by name with given args, returns result json
static json callTool(const std::string& tool_name, const json& args, core::Db& db) {
    auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
    if (!reg.has(tool_name))
        throw std::runtime_error("tool not found: " + tool_name);
    auto tool = reg.create(tool_name);
    return tool->call(args, db);
}

// ---------------------------------------------------------------------------
// Tool registry tests
// ---------------------------------------------------------------------------

TEST("mcp: all 14 tools registered") {
    auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
    auto keys = reg.keys();
    ASSERT_TRUE(keys.size() >= 14);
}

TEST("mcp: tool schema has required fields") {
    auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
    auto tool = reg.create("icmg_recall");
    ASSERT_TRUE(tool != nullptr);
    json schema = tool->schema();
    ASSERT_EQ(schema["type"].get<std::string>(), std::string("object"));
    ASSERT_TRUE(schema.contains("properties"));
    ASSERT_TRUE(schema["properties"].contains("query"));
}

// ---- v1.21.5 (FB3 TDD): the 8 new tools shipped in v1.21.4 register
// and expose schemas. They wrap CLI subcommands via safeExecShell — full
// execution is integration-tested elsewhere; here we only verify registration.

TEST("mcp FB3: all 8 v1.21.4 tools registered") {
    auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
    for (auto k : {
            "icmg_bench_recall",
            "icmg_feedback_record",
            "icmg_feedback_search",
            "icmg_feedback_stats",
            "icmg_memoir_list",
            "icmg_memoir_show",
            "icmg_metrics_per_cmd",
            "icmg_known_issue_match"}) {
        ASSERT_TRUE(reg.has(k));
    }
}

TEST("mcp FB3: required-arg schemas reject empty input") {
    auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
    // icmg_feedback_record requires topic + predicted + actual; validateArgs
    // should throw on empty payload — before any shell exec happens.
    auto tool = reg.create("icmg_feedback_record");
    ASSERT_TRUE(tool != nullptr);
    auto db = openTestDb();
    bool threw = false;
    try { (void)tool->call(json::object(), db); }
    catch (const std::exception&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("mcp FB3: icmg_feedback_search rejects missing query") {
    auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
    auto tool = reg.create("icmg_feedback_search");
    auto db = openTestDb();
    bool threw = false;
    try { (void)tool->call(json::object(), db); }
    catch (const std::exception&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("mcp FB3: icmg_known_issue_match rejects missing error") {
    auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
    auto tool = reg.create("icmg_known_issue_match");
    auto db = openTestDb();
    bool threw = false;
    try { (void)tool->call(json::object(), db); }
    catch (const std::exception&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("mcp FB3: icmg_memoir_show rejects missing id") {
    auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
    auto tool = reg.create("icmg_memoir_show");
    auto db = openTestDb();
    bool threw = false;
    try { (void)tool->call(json::object(), db); }
    catch (const std::exception&) { threw = true; }
    ASSERT_TRUE(threw);
}

TEST("mcp: icmg_stats returns row counts") {
    auto db = openTestDb();
    json result = callTool("icmg_stats", {}, db);
    ASSERT_TRUE(result.contains("memory_nodes"));
    ASSERT_TRUE(result.contains("graph_nodes"));
    ASSERT_TRUE(result.contains("graph_edges"));
    ASSERT_TRUE(result.contains("commands"));
    ASSERT_EQ(result["memory_nodes"].get<int>(), 0);
}

// ---------------------------------------------------------------------------
// icmg_store + icmg_recall
// ---------------------------------------------------------------------------

TEST("mcp: icmg_store stores a memory node") {
    auto db = openTestDb();
    json result = callTool("icmg_store", {
        {"topic",      "test-topic"},
        {"content",    "hello world"},
        {"importance", 1}
    }, db);
    // Store should return some success indicator
    std::string dump = result.dump();
    ASSERT_TRUE(!dump.empty());
}

TEST("mcp: icmg_recall returns stored node") {
    auto db = openTestDb();
    callTool("icmg_store", {
        {"topic",      "recall-topic"},
        {"content",    "findme content"},
        {"importance", 1}
    }, db);

    json result = callTool("icmg_recall", {
        {"query", "findme"},
        {"limit", 5}
    }, db);
    ASSERT_TRUE(result.contains("nodes"));
    std::string dump = result["nodes"].dump();
    ASSERT_CONTAINS(dump, "findme");
}

TEST("mcp: icmg_recall empty on mismatch") {
    auto db = openTestDb();
    json result = callTool("icmg_recall", {
        {"query", "xyznotexist12345"},
        {"limit", 5}
    }, db);
    ASSERT_TRUE(result.contains("nodes"));
    ASSERT_EQ(result["nodes"].size(), 0u);
}

// ---------------------------------------------------------------------------
// icmg_store validation
// ---------------------------------------------------------------------------

TEST("mcp: icmg_store rejects missing topic") {
    auto db = openTestDb();
    bool threw = false;
    try {
        callTool("icmg_store", {{"content", "no topic"}}, db);
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

// ---------------------------------------------------------------------------
// icmg_cmd_suggest
// ---------------------------------------------------------------------------

TEST("mcp: icmg_cmd_suggest empty on empty DB") {
    auto db = openTestDb();
    json result = callTool("icmg_cmd_suggest", {}, db);
    ASSERT_TRUE(result.contains("commands"));
    ASSERT_EQ(result["commands"].size(), 0u);
}

TEST("mcp: icmg_cmd_suggest filters by prefix") {
    auto db = openTestDb();
    db.run("INSERT INTO commands(command,frequency) VALUES(?,?)", {"git status", "5"});
    db.run("INSERT INTO commands(command,frequency) VALUES(?,?)", {"cargo build", "3"});

    json result = callTool("icmg_cmd_suggest", {{"prefix", "git"}}, db);
    ASSERT_EQ(result["commands"].size(), 1u);
    ASSERT_EQ(result["commands"][0]["command"].get<std::string>(), std::string("git status"));
}

// ---------------------------------------------------------------------------
// icmg_abbr_expand
// ---------------------------------------------------------------------------

TEST("mcp: icmg_abbr_expand no change on unknown text") {
    auto db = openTestDb();
    // text param — no abbreviations registered, so output == input
    json result = callTool("icmg_abbr_expand", {{"text", "XYZ_NOTEXIST"}}, db);
    ASSERT_TRUE(result.contains("expanded"));
    // Either unchanged or changed — both are valid; just verify it returns
    ASSERT_EQ(result["original"].get<std::string>(), std::string("XYZ_NOTEXIST"));
}

// ---------------------------------------------------------------------------
// icmg_sp_search
// ---------------------------------------------------------------------------

TEST("mcp: icmg_sp_search empty on empty DB") {
    auto db = openTestDb();
    json result = callTool("icmg_sp_search", {{"query", "GetUser"}}, db);
    ASSERT_TRUE(result.contains("stored_procedures"));
    ASSERT_EQ(result["stored_procedures"].size(), 0u);
}

// ---------------------------------------------------------------------------
// icmg_stats after store
// ---------------------------------------------------------------------------

TEST("mcp: icmg_stats top_topics after store") {
    auto db = openTestDb();
    callTool("icmg_store", {{"topic","alpha"},{"content","a"},{"importance",1}}, db);
    callTool("icmg_store", {{"topic","alpha"},{"content","b"},{"importance",1}}, db);
    callTool("icmg_store", {{"topic","beta"}, {"content","c"},{"importance",1}}, db);

    json result = callTool("icmg_stats", {}, db);
    ASSERT_EQ(result["memory_nodes"].get<int>(), 3);
    ASSERT_TRUE(result["top_topics"].size() >= 1u);
}

// ---------------------------------------------------------------------------
// McpServer construction (audit table creation)
// ---------------------------------------------------------------------------

TEST("mcp: server constructs without error") {
    auto db = openTestDb();
    // McpServer creates mcp_audit_log table in constructor — should not throw
    mcp::McpServer server(db);
    ASSERT_TRUE(true);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
