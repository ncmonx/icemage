#pragma once
#include "../core/db.hpp"
#include <string>
#include <nlohmann/json.hpp>

namespace icmg::mcp {

using json = nlohmann::json;

class McpServer {
public:
    explicit McpServer(core::Db& db);

    // Main stdio loop. Reads newline-delimited JSON-RPC requests,
    // dispatches, writes responses. Returns when stdin closes.
    void run();

    // Public for unit tests — parses icmg:// URI, dispatches to relevant store,
    // returns JSON content. Throws on invalid scheme / missing record.
    json readResourceUri(const std::string& uri);

    // Returns the tools/list response JSON (callable for tests without stdin loop).
    // lazy=true: minimal schema + truncated description (<=160 chars + "...").
    // lazy=false: full schema + full description (existing behavior).
    json buildToolsListResponse(bool lazy) const;

private:
    core::Db& db_;
    bool initialized_ = false;

    void handleRequest(const json& req);
    void handleInitialize(const json& req);
    void handleListTools(const json& req);
    void handleCallTool(const json& req);

    // Phase 23 Task 4: resources protocol.
    void handleListResources(const json& req);
    void handleReadResource(const json& req);

    void sendResponse(const json& id, const json& result);
    void sendError(const json& id, int code, const std::string& msg);

    // A6: audit log
    void logAudit(const std::string& tool_name, const std::string& summary);

    // Ensure mcp_audit_log table exists
    void ensureAuditTable();
};

} // namespace icmg::mcp
