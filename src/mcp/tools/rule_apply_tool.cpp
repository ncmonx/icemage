#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../rules/rule_store.hpp"

namespace icmg::mcp {

class RuleApplyTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_rule_apply"; }
    std::string description() const override {
        return "Get all rules applicable to a file path (inherited from parent scopes).";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"file_path", "string", "File path to apply rules for", true},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "file_path", 2000);
        if (getStr(args, "file_path").find('\0') != std::string::npos)
            throw McpError(-32602, "file_path contains null byte");
    }

    json callImpl(const json& args, core::Db& db) override {
        std::string path = getStr(args, "file_path");

        rules::RuleStore store(db);
        auto applicable = store.forPath(path);

        json arr = json::array();
        for (auto& r : applicable) {
            arr.push_back({
                {"id",         r.id},
                {"scope_path", r.scope_path},
                {"rule_type",  r.rule_type},
                {"name",       r.name},
                {"content",    r.content},
                {"priority",   r.priority}
            });
        }
        return {{"rules", arr}, {"count", (int)arr.size()}, {"file_path", path}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_rule_apply", RuleApplyTool);

} // namespace icmg::mcp
