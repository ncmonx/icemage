// v1.21.4 (FB3 #2): icmg_feedback_record MCP tool.
// Records a correction (predicted vs actual) into the feedbacks table.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class FeedbackRecordTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_feedback_record"; }
    std::string description() const override {
        return "Record a correction: the AI predicted Y, the actual was Z. "
               "Stored under a topic for future recall via icmg_feedback_search.";
    }
    bool isMutating() const override { return true; }
    std::vector<McpToolParam> params() const override {
        return {
            {"topic",     "string", "Short topic key (e.g. \"build-rule\")", true},
            {"predicted", "string", "What the AI said / predicted",          true},
            {"actual",    "string", "What was actually correct",             true},
            {"note",      "string", "Optional context note",                 false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        for (auto k : {"topic", "predicted", "actual"}) {
            if (!args.contains(k) || !args[k].is_string() || args[k].get<std::string>().empty())
                throw std::invalid_argument(std::string("missing required arg: ") + k);
        }
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" feedback-loop record"
            " --topic \""     + args["topic"].get<std::string>() + "\""
            " --predicted \"" + args["predicted"].get<std::string>() + "\""
            " --actual \""    + args["actual"].get<std::string>() + "\"";
        if (args.contains("note") && args["note"].is_string())
            cmd += " --note \"" + args["note"].get<std::string>() + "\"";
        auto res = core::safeExecShell(cmd, false, 30000);
        return {{"exit_code", res.exit_code}, {"stdout", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_feedback_record", FeedbackRecordTool);

} // namespace icmg::mcp
