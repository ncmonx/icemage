// v1.21.4 (FB3 #3): icmg_feedback_search MCP tool.
// Substring scan across feedbacks(topic + predicted + actual).

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class FeedbackSearchTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_feedback_search"; }
    std::string description() const override {
        return "Substring scan across feedback entries (topic + predicted + actual). "
               "Use BEFORE generating a similar prediction to catch known corrections.";
    }
    std::vector<McpToolParam> params() const override {
        return {{"query", "string", "Substring to match", true}};
    }

protected:
    void validateArgs(const json& args) override {
        if (!args.contains("query") || !args["query"].is_string()
            || args["query"].get<std::string>().empty())
            throw std::invalid_argument("missing required arg: query");
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" feedback-loop search \""
                        + args["query"].get<std::string>() + "\"";
        auto res = core::safeExecShell(cmd, false, 30000);
        return {{"exit_code", res.exit_code}, {"results", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_feedback_search", FeedbackSearchTool);

} // namespace icmg::mcp
