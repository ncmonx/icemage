#include "../test_main.hpp"
#include "../../src/mcp/tool_annotations.hpp"
#include "../../src/mcp/server.hpp"
#include "../../src/core/db.hpp"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;
using namespace icmg;
using icmg::mcp::McpToolAnnotations;
using icmg::mcp::defaultToolAnnotations;
using icmg::mcp::toolAnnotationsToJson;

// ---- pure: default derivation from isMutating() ----------------------------

TEST("annotations: read-only tool defaults") {
    auto a = defaultToolAnnotations(false);
    ASSERT_TRUE(a.readOnly);
    ASSERT_TRUE(a.idempotent);
    ASSERT_TRUE(!a.destructive);
    ASSERT_TRUE(!a.openWorld);
}

TEST("annotations: mutating tool is not read-only, not idempotent") {
    auto a = defaultToolAnnotations(true);
    ASSERT_TRUE(!a.readOnly);
    ASSERT_TRUE(!a.idempotent);
    ASSERT_TRUE(!a.destructive);   // additive, not destructive
}

// ---- pure: json shape ------------------------------------------------------

TEST("annotations json: emits all four hints, boolean typed") {
    auto j = toolAnnotationsToJson(defaultToolAnnotations(false));
    ASSERT_TRUE(j.contains("readOnlyHint")    && j["readOnlyHint"].is_boolean());
    ASSERT_TRUE(j.contains("destructiveHint") && j["destructiveHint"].is_boolean());
    ASSERT_TRUE(j.contains("idempotentHint")  && j["idempotentHint"].is_boolean());
    ASSERT_TRUE(j.contains("openWorldHint")   && j["openWorldHint"].is_boolean());
    ASSERT_EQ(j["readOnlyHint"].get<bool>(), true);
}

TEST("annotations json: title omitted when empty, present when set") {
    auto j0 = toolAnnotationsToJson(defaultToolAnnotations(false));
    ASSERT_TRUE(!j0.contains("title"));
    McpToolAnnotations a; a.title = "Recall";
    auto j1 = toolAnnotationsToJson(a);
    ASSERT_TRUE(j1.contains("title"));
    ASSERT_EQ(j1["title"].get<std::string>(), std::string("Recall"));
}

// ---- integration: tools/list carries annotations ---------------------------

static core::Db openTestDb() {
    core::Db db(":memory:");
    db.run("CREATE TABLE memory_nodes(id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " topic TEXT NOT NULL, content TEXT NOT NULL, keywords TEXT,"
           " importance INTEGER NOT NULL DEFAULT 1, frequency INTEGER NOT NULL DEFAULT 1,"
           " last_used INTEGER, expires_at INTEGER, deleted_at INTEGER,"
           " created_by TEXT NOT NULL DEFAULT '', row_version INTEGER NOT NULL DEFAULT 0,"
           " zone TEXT NOT NULL DEFAULT 'default', pinned INTEGER NOT NULL DEFAULT 0,"
           " git_sha TEXT NOT NULL DEFAULT '',"
           " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
    db.run("CREATE TABLE commands(id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " command TEXT NOT NULL UNIQUE, frequency INTEGER NOT NULL DEFAULT 1,"
           " last_used INTEGER, avg_lines INTEGER NOT NULL DEFAULT 0, tags TEXT)");
    db.run("CREATE TABLE graph_nodes(id INTEGER PRIMARY KEY AUTOINCREMENT,"
           " path TEXT NOT NULL UNIQUE, lang TEXT, context TEXT, symbols TEXT,"
           " size_bytes INTEGER, file_hash TEXT, access_count INTEGER NOT NULL DEFAULT 0,"
           " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           " zone TEXT NOT NULL DEFAULT 'default')");
    return db;
}

// Find a tool object by name in the tools/list array.
static json findTool(const json& tools, const std::string& name) {
    for (const auto& t : tools) {
        if (t.contains("name") && t["name"].get<std::string>() == name) return t;
    }
    return json();
}

TEST("tools/list: every tool carries an annotations object with the 4 hints") {
    auto db = openTestDb();
    mcp::McpServer server(db);
    json result = server.buildToolsListResponse(false);
    ASSERT_TRUE(result["tools"].size() > 0u);
    for (const auto& t : result["tools"]) {
        ASSERT_TRUE(t.contains("annotations"));
        const auto& an = t["annotations"];
        ASSERT_TRUE(an.contains("readOnlyHint")    && an["readOnlyHint"].is_boolean());
        ASSERT_TRUE(an.contains("destructiveHint") && an["destructiveHint"].is_boolean());
        ASSERT_TRUE(an.contains("idempotentHint")  && an["idempotentHint"].is_boolean());
        ASSERT_TRUE(an.contains("openWorldHint")   && an["openWorldHint"].is_boolean());
    }
}

TEST("tools/list: mutating icmg_store is not read-only; recall is read-only") {
    auto db = openTestDb();
    mcp::McpServer server(db);
    json tools = server.buildToolsListResponse(false)["tools"];

    json store = findTool(tools, "icmg_store");
    ASSERT_TRUE(!store.is_null());
    ASSERT_EQ(store["annotations"]["readOnlyHint"].get<bool>(), false);

    json recall = findTool(tools, "icmg_recall");
    ASSERT_TRUE(!recall.is_null());
    ASSERT_EQ(recall["annotations"]["readOnlyHint"].get<bool>(), true);
}

TEST("tools/list: fetch/ingest/sync flagged openWorld") {
    auto db = openTestDb();
    mcp::McpServer server(db);
    json tools = server.buildToolsListResponse(false)["tools"];
    for (const char* n : {"icmg_fetch", "icmg_ingest", "icmg_sync"}) {
        json t = findTool(tools, n);
        ASSERT_TRUE(!t.is_null());
        ASSERT_EQ(t["annotations"]["openWorldHint"].get<bool>(), true);
    }
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
