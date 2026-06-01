// v1.24.0 (P5): icmg_port_apply MCP tool.
// Exposes cross-project bundle apply to MCP clients. Export is NOT exposed —
// file selection is a deliberate dev gesture, not an AI auto-fire path.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/path_utils.hpp"   // v1.65 S1: isSafeToolPath
#include <cstdlib>

namespace icmg::mcp {

class PortApplyTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_port_apply"; }
    std::string description() const override {
        return "Apply a `.icmg-port` cross-project bundle into the current project. "
               "Default dry-run shows planned writes + conflicts; pass write=true "
               "to mutate (overwrite=true also replaces existing with .bak-<ts>).";
    }
    bool isMutating() const override { return true; }
    std::vector<McpToolParam> params() const override {
        return {
            {"artifact",  "string",  "Path to .icmg-port artifact",                  true},
            {"to",        "string",  "Target directory under which to apply files",  true},
            {"path_map",  "string",  "Optional `from=to` path prefix rewrite",       false},
            {"write",     "boolean", "If true, mutate (skip conflicts unless overwrite)", false},
            {"overwrite", "boolean", "If true, overwrite conflicts (backs up to .bak-<ts>)", false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        for (auto k : {"artifact", "to"}) {
            if (!args.contains(k) || !args[k].is_string()
                || args[k].get<std::string>().empty()) {
                throw std::invalid_argument(std::string("missing required arg: ") + k);
            }
            // v1.65 S1: reject shell-meta + traversal before shell interpolation.
            if (!core::isSafeToolPath(args[k].get<std::string>())) {
                throw std::invalid_argument(
                    std::string("unsafe path (shell-meta/traversal) in arg: ") + k);
            }
        }
        if (args.contains("path_map") && args["path_map"].is_string()
            && !core::isSafeToolPath(args["path_map"].get<std::string>())) {
            throw std::invalid_argument("unsafe path_map (shell-meta/traversal)");
        }
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" port apply \""
                        + args["artifact"].get<std::string>() + "\""
                        + " --to \"" + args["to"].get<std::string>() + "\"";
        if (args.contains("path_map") && args["path_map"].is_string()) {
            cmd += " --path-map \"" + args["path_map"].get<std::string>() + "\"";
        }
        if (args.value("write", false)) cmd += " --write";
        if (args.value("overwrite", false)) cmd += " --overwrite";
        auto res = core::safeExecShell(cmd, false, 60000);
        return {{"exit_code", res.exit_code}, {"stdout", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_port_apply", PortApplyTool);

}  // namespace icmg::mcp
