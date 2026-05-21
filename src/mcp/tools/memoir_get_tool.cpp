// v1.21.4 (FB3 #6): icmg_memoir_get MCP tool.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class MemoirGetTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_memoir_show"; }
    std::string description() const override {
        return "Fetch a single memoir entry by id (long-form ADR / post-mortem text).";
    }
    std::vector<McpToolParam> params() const override {
        return {{"id", "integer", "Memoir id (from icmg_memoir_list)", true}};
    }

protected:
    void validateArgs(const json& args) override {
        if (!args.contains("id") || !args["id"].is_number_integer())
            throw std::invalid_argument("missing required arg: id");
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" memoir show "
                        + std::to_string(args["id"].get<int>());
        auto res = core::safeExecShell(cmd, false, 30000);
        return {{"exit_code", res.exit_code}, {"entry", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_memoir_show", MemoirGetTool);

} // namespace icmg::mcp
