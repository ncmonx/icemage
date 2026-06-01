// v1.21.4 (FB3 #5): icmg_memoir_list MCP tool.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class MemoirListTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_memoir_list"; }
    std::string description() const override {
        return "List long-form memoir entries (ADRs, post-mortems) most recent first.";
    }
    std::vector<McpToolParam> params() const override {
        return {{"limit", "integer", "Max entries (default 20)", false}};
    }

protected:
    json callImpl(const json& args, core::Db& /*db*/) override {
        int lim = args.contains("limit") && args["limit"].is_number_integer()
                  ? args["limit"].get<int>() : 20;
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" memoir list --limit " + std::to_string(lim);
        auto res = core::safeExecShell(cmd, false, 30000);
        return {{"exit_code", res.exit_code}, {"entries", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_memoir_list", MemoirListTool);

} // namespace icmg::mcp
