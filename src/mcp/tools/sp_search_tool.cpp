#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../sp/sp_store.hpp"

namespace icmg::mcp {

class SpSearchTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_sp_search"; }
    std::string description() const override {
        return "Search stored procedures by name, context, or tables used.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"query",   "string",  "Search term",                  true},
            {"limit",   "integer", "Max results (default 10)",      false},
            {"db_type", "string",  "Filter by DB type (mssql/etc)", false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "query", 500);
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string query   = getStr(args, "query");
        int limit           = getInt(args, "limit", 10);
        std::string db_type = getStr(args, "db_type");
        limit = std::max(1, std::min(50, limit));

        sp::SpStore store(db);
        auto sps = store.search(query, limit);

        // Apply db_type filter post-query if specified
        if (!db_type.empty()) {
            sps.erase(std::remove_if(sps.begin(), sps.end(),
                [&](const sp::StoredProcedure& s){ return s.db_type != db_type; }),
                sps.end());
        }

        json arr = json::array();
        for (auto& s : sps) {
            arr.push_back({
                {"id",            s.id},
                {"name",          s.name},
                {"db_type",       s.db_type},
                {"database_name", s.database_name},
                {"context",       s.context},
                {"version",       s.version},
                {"tables_used",   s.tables_used}
            });
        }
        return {{"stored_procedures", arr}, {"count", (int)arr.size()}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_sp_search", SpSearchTool);

} // namespace icmg::mcp
