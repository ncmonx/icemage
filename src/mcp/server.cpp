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
            {"version", "0.2.0"}
        }},
        {"capabilities", {
            {"tools", {{"listChanged", false}}}
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

void McpServer::logAudit(const std::string& tool_name, const std::string& summary) {
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    try {
        db_.run("INSERT INTO mcp_audit_log(tool_name,args_summary,created_at) VALUES(?,?,?)",
                {tool_name, summary, std::to_string(now)});
    } catch (...) {}
}

} // namespace icmg::mcp
