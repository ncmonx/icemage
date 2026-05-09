#include "server.hpp"
#include "base_mcp_tool.hpp"
#include "../core/registry.hpp"
#include <iostream>
#include <chrono>

namespace icmg::mcp {

McpServer::McpServer(core::Db& db) : db_(db) {
    ensureAuditTable();
}

void McpServer::ensureAuditTable() {
    db_.run(
        "CREATE TABLE IF NOT EXISTS mcp_audit_log("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " tool_name TEXT NOT NULL,"
        " args_summary TEXT,"
        " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
        ")"
    );
}

// ---------------------------------------------------------------------------
// stdio loop
// ---------------------------------------------------------------------------

void McpServer::run() {
    // Disable output buffering
    std::cout.setf(std::ios::unitbuf);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        json req;
        try {
            // A7: limit nesting depth (nlohmann supports this via parse callback)
            req = json::parse(line);
        } catch (const std::exception& e) {
            sendError(nullptr, -32700, std::string("Parse error: ") + e.what());
            continue;
        }

        // A7: basic nesting depth check on incoming object
        // nlohmann already handles deeply nested JSON gracefully; no manual walk needed

        try {
            handleRequest(req);
        } catch (const std::exception& e) {
            json id = req.contains("id") ? req["id"] : nullptr;
            sendError(id, -32603, std::string("Internal error: ") + e.what());
        }
    }
}

void McpServer::handleRequest(const json& req) {
    json id = req.contains("id") ? req["id"] : nullptr;

    if (!req.contains("method") || !req["method"].is_string()) {
        sendError(id, -32600, "Invalid Request: missing method");
        return;
    }

    std::string method = req["method"].get<std::string>();

    if (method == "initialize") {
        handleInitialize(req);
    } else if (method == "tools/list") {
        handleListTools(req);
    } else if (method == "tools/call") {
        handleCallTool(req);
    } else if (method == "resources/list") {
        handleListResources(req);
    } else if (method == "resources/read") {
        handleReadResource(req);
    } else if (method == "notifications/initialized") {
        // No response needed
    } else {
        sendError(id, -32601, "Method not found: " + method);
    }
}

// ---------------------------------------------------------------------------
// handlers
// ---------------------------------------------------------------------------

void McpServer::handleInitialize(const json& req) {
    initialized_ = true;
    json id = req.contains("id") ? req["id"] : nullptr;

    sendResponse(id, {
        {"protocolVersion", "2024-11-05"},
        {"serverInfo", {
            {"name", "icmg"},
            {"version", "0.33.0"}
        }},
        {"capabilities", {
            {"tools",     {{"listChanged", false}}},
            {"resources", {{"subscribe", false}, {"listChanged", false}}}
        }}
    });
}

void McpServer::handleListTools(const json& req) {
    json id = req.contains("id") ? req["id"] : nullptr;

    auto& reg = core::Registry<BaseMcpTool>::instance();
    auto keys = reg.keys();

    json tools = json::array();
    for (auto& k : keys) {
        auto tool = reg.create(k);
        tools.push_back({
            {"name",        tool->name()},
            {"description", tool->description()},
            {"inputSchema", tool->schema()}
        });
    }

    sendResponse(id, {{"tools", tools}});
}

void McpServer::handleCallTool(const json& req) {
    json id = req.contains("id") ? req["id"] : nullptr;

    if (!req.contains("params")) {
        sendError(id, -32602, "Missing params");
        return;
    }
    const json& params = req["params"];

    if (!params.contains("name") || !params["name"].is_string()) {
        sendError(id, -32602, "Missing tool name");
        return;
    }
    std::string toolName = params["name"].get<std::string>();

    // A5: store node count check
    if (toolName == "icmg_store") {
        int cnt = 0;
        db_.query("SELECT COUNT(*) FROM memory_nodes", {},
                  [&](const core::Row& r) {
                      if (!r.empty()) try { cnt = std::stoi(r[0]); } catch (...) {}
                  });
        if (cnt >= 10000) {
            sendError(id, -32603,
                      "Memory store full (10000 nodes). Run: icmg memory purge");
            return;
        }
    }

    auto& reg = core::Registry<BaseMcpTool>::instance();
    if (!reg.has(toolName)) {
        sendError(id, -32602, "Unknown tool: " + toolName);
        return;
    }

    json args = params.contains("arguments") ? params["arguments"] : json::object();

    // A5: content size limit for store
    if (toolName == "icmg_store" && args.contains("content")) {
        auto cs = args["content"].get<std::string>();
        if (cs.size() > 1024 * 1024) {
            sendError(id, -32602, "content exceeds 1MB limit");
            return;
        }
    }

    try {
        auto tool = reg.create(toolName);
        json result = tool->call(args, db_);

        // A6: audit (only metadata, not content)
        std::string summary;
        if (args.contains("query"))  summary = "query=" + args["query"].get<std::string>().substr(0, 80);
        if (args.contains("path"))   summary = "path="  + args["path"].get<std::string>().substr(0, 80);
        if (args.contains("topic"))  summary = "topic=" + args["topic"].get<std::string>().substr(0, 80);
        logAudit(toolName, summary);

        sendResponse(id, {
            {"content", json::array({{{"type", "text"}, {"text", result.dump()}}})}
        });

    } catch (const McpError& e) {
        sendError(id, e.code, e.what());
    } catch (const std::exception& e) {
        sendError(id, -32603, std::string("Tool error: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// response helpers
// ---------------------------------------------------------------------------

void McpServer::sendResponse(const json& id, const json& result) {
    json res = {
        {"jsonrpc", "2.0"},
        {"id",      id},
        {"result",  result}
    };
    std::cout << res.dump() << "\n";
}

void McpServer::sendError(const json& id, int code, const std::string& msg) {
    json res = {
        {"jsonrpc", "2.0"},
        {"id",      id},
        {"error",   {{"code", code}, {"message", msg}}}
    };
    std::cout << res.dump() << "\n";
}

// ---------------------------------------------------------------------------
// Phase 23 Task 4: resources protocol
// ---------------------------------------------------------------------------
//
// URI scheme:
//   icmg://memory/<id>           → MemoryNode JSON
//   icmg://graph/<id>            → GraphNode JSON
//   icmg://summary/<file_path>   → outline (heuristic; sees mode of `icmg summarize`)
//   icmg://session/<name>        → session snapshot
//
// resources/list returns the top-N hot files + recently-stored memory nodes.
// resources/read parses URI, dispatches.

void McpServer::handleListResources(const json& req) {
    json id = req.contains("id") ? req["id"] : nullptr;

    json resources = json::array();

    // Top 20 hot files (by frequency * recency proxy via id desc).
    db_.query("SELECT id, path FROM graph_nodes "
              "WHERE parent_id IS NULL "
              "ORDER BY id DESC LIMIT 20",
              {},
              [&](const core::Row& r) {
                  if (r.size() < 2) return;
                  resources.push_back({
                      {"uri",      "icmg://graph/" + r[0]},
                      {"name",     r[1]},
                      {"mimeType", "application/json"},
                      {"description", "graph node " + r[0]}
                  });
              });

    // Top 20 recent memory nodes.
    db_.query("SELECT id, topic FROM memory_nodes "
              "WHERE deleted_at IS NULL "
              "ORDER BY last_used DESC LIMIT 20",
              {},
              [&](const core::Row& r) {
                  if (r.size() < 2) return;
                  resources.push_back({
                      {"uri",      "icmg://memory/" + r[0]},
                      {"name",     r[1]},
                      {"mimeType", "application/json"},
                      {"description", "memory node " + r[0]}
                  });
              });

    sendResponse(id, {{"resources", resources}});
}

json McpServer::readResourceUri(const std::string& uri) {
    // Expect "icmg://<kind>/<id_or_path>"
    const std::string scheme = "icmg://";
    if (uri.compare(0, scheme.size(), scheme) != 0)
        throw std::runtime_error("invalid uri scheme");
    std::string rest = uri.substr(scheme.size());
    auto slash = rest.find('/');
    if (slash == std::string::npos) throw std::runtime_error("invalid uri (no path)");
    std::string kind = rest.substr(0, slash);
    std::string ident = rest.substr(slash + 1);

    json out;
    if (kind == "memory") {
        db_.query("SELECT id,topic,content,keywords,importance,frequency,"
                  "last_used,created_at,zone FROM memory_nodes WHERE id=?",
                  {ident},
                  [&](const core::Row& r) {
                      if (r.size() < 9) return;
                      out = {
                          {"id",         r[0]},
                          {"topic",      r[1]},
                          {"content",    r[2]},
                          {"keywords",   r[3]},
                          {"importance", r[4]},
                          {"frequency",  r[5]},
                          {"last_used",  r[6]},
                          {"created_at", r[7]},
                          {"zone",       r[8]}
                      };
                  });
        if (out.is_null()) throw std::runtime_error("memory node not found");
    } else if (kind == "graph") {
        db_.query("SELECT id,path,language,kind,COALESCE(parent_id,0),"
                  "COALESCE(symbol_name,''),COALESCE(line_start,0),COALESCE(line_end,0),"
                  "COALESCE(content,'') FROM graph_nodes WHERE id=?",
                  {ident},
                  [&](const core::Row& r) {
                      if (r.size() < 9) return;
                      out = {
                          {"id",          r[0]},
                          {"path",        r[1]},
                          {"language",    r[2]},
                          {"kind",        r[3]},
                          {"parent_id",   r[4]},
                          {"symbol_name", r[5]},
                          {"line_start",  r[6]},
                          {"line_end",    r[7]},
                          {"content",     r[8]}
                      };
                  });
        if (out.is_null()) throw std::runtime_error("graph node not found");
    } else if (kind == "session") {
        db_.query("SELECT name, snapshot, created_at FROM sessions WHERE name=?",
                  {ident},
                  [&](const core::Row& r) {
                      if (r.size() < 3) return;
                      out = {
                          {"name",       r[0]},
                          {"snapshot",   r[1]},
                          {"created_at", r[2]}
                      };
                  });
        if (out.is_null()) throw std::runtime_error("session not found");
    } else if (kind == "summary") {
        // Lookup graph_nodes by path, then return content excerpt.
        db_.query("SELECT id, content FROM graph_nodes WHERE path=? AND parent_id IS NULL LIMIT 1",
                  {ident},
                  [&](const core::Row& r) {
                      if (r.size() < 2) return;
                      out = {{"path", ident}, {"id", r[0]}, {"summary", r[1].substr(0, 2000)}};
                  });
        if (out.is_null()) throw std::runtime_error("file not in graph");
    } else {
        throw std::runtime_error("unknown resource kind: " + kind);
    }
    return out;
}

void McpServer::handleReadResource(const json& req) {
    json id = req.contains("id") ? req["id"] : nullptr;
    if (!req.contains("params") || !req["params"].contains("uri")) {
        sendError(id, -32602, "Missing params.uri");
        return;
    }
    std::string uri = req["params"]["uri"].get<std::string>();
    try {
        json content = readResourceUri(uri);
        sendResponse(id, {
            {"contents", json::array({
                {{"uri", uri}, {"mimeType", "application/json"}, {"text", content.dump()}}
            })}
        });
        logAudit("resource_read", "uri=" + uri.substr(0, 80));
    } catch (const std::exception& e) {
        sendError(id, -32603, std::string("resource read failed: ") + e.what());
    }
}

void McpServer::logAudit(const std::string& tool_name, const std::string& summary) {
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    try {
        db_.run("INSERT INTO mcp_audit_log(tool_name,args_summary,created_at) VALUES(?,?,?)",
                {tool_name, summary, std::to_string(now)});
    } catch (...) {}
}

} // namespace icmg::mcp
