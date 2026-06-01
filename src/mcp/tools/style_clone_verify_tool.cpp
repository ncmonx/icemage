// v1.22.0 (SC5): icmg_style_clone_verify MCP tool.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class StyleCloneVerifyTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_style_clone_verify"; }
    std::string description() const override {
        return "Check which files matching <glob> are still in sync with the "
               "stored pattern's structural hash vs drifted since last apply.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"name", "string", "Pattern name",                             true},
            {"glob", "string", "Target file glob (e.g. \"menus/*.vue\")",  true},
        };
    }

protected:
    void validateArgs(const json& args) override {
        for (auto k : {"name", "glob"}) {
            if (!args.contains(k) || !args[k].is_string()
                || args[k].get<std::string>().empty())
                throw std::invalid_argument(std::string("missing required arg: ") + k);
        }
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" style-clone verify "
                        + args["name"].get<std::string>()
                        + " --glob \"" + args["glob"].get<std::string>() + "\"";
        auto res = core::safeExecShell(cmd, false, 60000);
        return {{"exit_code", res.exit_code}, {"output", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_style_clone_verify", StyleCloneVerifyTool);

} // namespace icmg::mcp
