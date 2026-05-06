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

private:
    core::Db& db_;
    bool initialized_ = false;

    void handleRequest(const json& req);
    void handleInitialize(const json& req);
    void handleListTools(const json& req);
    void handleCallTool(const json& req);

    void sendResponse(const json& id, const json& result);
    void sendError(const json& id, int code, const std::string& msg);

    // A6: audit log
    void logAudit(const std::string& tool_name, const std::string& summary);

    // Ensure mcp_audit_log table exists
    void ensureAuditTable();
};

} // namespace icmg::mcp
