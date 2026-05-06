#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../sp/sp_store.hpp"

namespace icmg::mcp {

class SpContextTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_sp_context"; }
    std::string description() const override {
        return "Get full details of a stored procedure: SQL content, parameters, tables used.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"sp_name", "string", "Stored procedure name", true},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "sp_name", 200);
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string name = getStr(args, "sp_name");

        sp::SpStore store(db);
        auto s = store.get(name);

        if (!s) return {{"found", false}, {"name", name}};

        json params = json::array();
        for (auto& p : s->parameters) {
            params.push_back({
                {"name",      p.name},
                {"type",      p.type},
                {"direction", p.direction},
                {"default",   p.default_val}
            });
        }

        return {
            {"found",          true},
            {"id",             s->id},
            {"name",           s->name},
            {"db_type",        s->db_type},
            {"database_name",  s->database_name},
            {"content",        s->content},
            {"context",        s->context},
            {"parameters",     params},
            {"return_type",    s->return_type},
            {"tables_used",    s->tables_used},
            {"sp_dependencies",s->sp_dependencies},
            {"version",        s->version}
        };
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_sp_context", SpContextTool);

} // namespace icmg::mcp
