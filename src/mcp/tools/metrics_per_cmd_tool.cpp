// v1.21.4 (FB3 #7): icmg_metrics_per_cmd MCP tool.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class MetricsPerCmdTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_metrics_per_cmd"; }
    std::string description() const override {
        return "Top filtered commands by absolute lines saved over the window. "
               "Shows which command types benefit most from Tkil noise stripping.";
    }
    std::vector<McpToolParam> params() const override {
        return {{"top", "integer", "Number of rows (default 10)", false}};
    }

protected:
    json callImpl(const json& args, core::Db& /*db*/) override {
        int top = args.contains("top") && args["top"].is_number_integer()
                  ? args["top"].get<int>() : 10;
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" metrics per-cmd --top " + std::to_string(top);
        auto res = core::safeExecShell(cmd, false, 30000);
        return {{"exit_code", res.exit_code}, {"rows", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_metrics_per_cmd", MetricsPerCmdTool);

} // namespace icmg::mcp
