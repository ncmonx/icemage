// v1.22.0 (SC5): icmg_style_clone_apply MCP tool.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class StyleCloneApplyTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_style_clone_apply"; }
    std::string description() const override {
        return "Apply a cached style pattern to files matching a glob. "
               "Default is dry-run (returns diff summary). Pass write=true "
               "to mutate target files in place.";
    }
    bool isMutating() const override { return true; }
    std::vector<McpToolParam> params() const override {
        return {
            {"name",  "string",  "Pattern name (from `style-clone extract --save-as`)", true},
            {"glob",  "string",  "Target file glob (e.g. \"menus/*.vue\")",              true},
            {"write", "boolean", "Apply changes (default false = dry-run)",              false},
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
        std::string cmd = "\"" + icmg + "\" style-clone apply "
                        + args["name"].get<std::string>()
                        + " --to \"" + args["glob"].get<std::string>() + "\"";
        if (args.value("write", false)) cmd += " --write";
        auto res = core::safeExecShell(cmd, false, 60000);
        return {{"exit_code", res.exit_code}, {"output", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_style_clone_apply", StyleCloneApplyTool);

} // namespace icmg::mcp
