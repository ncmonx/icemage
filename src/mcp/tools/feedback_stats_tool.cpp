// v1.21.4 (FB3 #4): icmg_feedback_stats MCP tool.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class FeedbackStatsTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_feedback_stats"; }
    std::string description() const override {
        return "Feedback aggregates: total count, by-topic breakdown, "
               "most-applied entries (proxies for actual usefulness).";
    }
    std::vector<McpToolParam> params() const override { return {}; }

protected:
    json callImpl(const json& /*args*/, core::Db& /*db*/) override {
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        auto res = core::safeExecShell("\"" + icmg + "\" feedback-loop stats", false, 30000);
        return {{"exit_code", res.exit_code}, {"stats", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_feedback_stats", FeedbackStatsTool);

} // namespace icmg::mcp
