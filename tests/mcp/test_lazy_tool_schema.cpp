#include "../test_main.hpp"
#include "../../src/mcp/server.hpp"
#include "../../src/mcp/base_mcp_tool.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/registry.hpp"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;
using namespace icmg;

// ---------------------------------------------------------------------------
// Helper: minimal in-memory DB (same tables as test_mcp.cpp)
// ---------------------------------------------------------------------------
static core::Db openTestDb() {
    core::Db db(":memory:");

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
    db.run(
        "CREATE TABLE commands("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " command TEXT NOT NULL UNIQUE,"
        " frequency INTEGER NOT NULL DEFAULT 1,"
        " last_used INTEGER,"
        " avg_lines INTEGER NOT NULL DEFAULT 0,"
        " tags TEXT)"
    );
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
    db.run(
        "CREATE TABLE graph_edges("
        " src INTEGER NOT NULL,"
        " dst INTEGER NOT NULL,"
        " edge_type TEXT NOT NULL,"
        " weight REAL NOT NULL DEFAULT 1.0,"
        " PRIMARY KEY (src, dst, edge_type))"
    );
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
    db.run(
        "CREATE TABLE query_history("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " query TEXT NOT NULL,"
        " matched_ids TEXT,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))"
    );
    db.run(
        "CREATE TABLE memory_keywords("
        " memory_id INTEGER NOT NULL,"
        " keyword TEXT NOT NULL,"
        " PRIMARY KEY (memory_id, keyword))"
    );
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST("default tools/list returns full schemas") {
    auto db = openTestDb();
    mcp::McpServer server(db);
    json result = server.buildToolsListResponse(false);

    ASSERT_TRUE(result.contains("tools"));
    ASSERT_TRUE(result["tools"].is_array());
    ASSERT_TRUE(result["tools"].size() > 0u);

    for (const auto& tool : result["tools"]) {
        ASSERT_TRUE(tool.contains("inputSchema"));
        // Full schema must have a "type" key
        ASSERT_TRUE(tool["inputSchema"].contains("type"));
    }
}

TEST("lazy tools/list returns empty inputSchema") {
    auto db = openTestDb();
    mcp::McpServer server(db);
    json result = server.buildToolsListResponse(true);

    ASSERT_TRUE(result.contains("tools"));
    ASSERT_TRUE(result["tools"].is_array());
    ASSERT_TRUE(result["tools"].size() > 0u);

    for (const auto& tool : result["tools"]) {
        ASSERT_TRUE(tool.contains("inputSchema"));
        json schema = tool["inputSchema"];
        // Must be exactly {"type":"object"} — no "properties", no "required"
        ASSERT_EQ(schema["type"].get<std::string>(), std::string("object"));
        ASSERT_TRUE(!schema.contains("properties"));
        ASSERT_TRUE(!schema.contains("required"));
    }
}

TEST("lazy descriptions truncated to <=160 chars") {
    auto db = openTestDb();
    mcp::McpServer server(db);
    json result = server.buildToolsListResponse(true);

    ASSERT_TRUE(result.contains("tools"));
    for (const auto& tool : result["tools"]) {
        ASSERT_TRUE(tool.contains("description"));
        std::string desc = tool["description"].get<std::string>();
        // Max allowed: 160 chars + "..." suffix = 163
        ASSERT_TRUE(desc.size() <= 163u);
    }
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
