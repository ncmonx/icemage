// v1.21.4 (FB3 #8): icmg_known_issue_recall MCP tool.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class KnownIssueMatchTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_known_issue_match"; }
    std::string description() const override {
        return "Match a known-issue / fix entry against an error text. Use BEFORE "
               "investigating an error pattern to surface prior resolutions.";
    }
    std::vector<McpToolParam> params() const override {
        return {{"error", "string", "Error text or symptom to match", true}};
    }

protected:
    void validateArgs(const json& args) override {
        if (!args.contains("error") || !args["error"].is_string()
            || args["error"].get<std::string>().empty())
            throw std::invalid_argument("missing required arg: error");
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" known-issue match \""
                        + args["error"].get<std::string>() + "\"";
        auto res = core::safeExecShell(cmd, false, 30000);
        return {{"exit_code", res.exit_code}, {"results", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_known_issue_match", KnownIssueMatchTool);

} // namespace icmg::mcp
