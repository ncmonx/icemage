// Phase 50 T4: icmg_savings MCP tool — telemetry rollup.
// Returns 30-day token-cost savings summary across compress, thinking,
// filter telemetry tables.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class SavingsTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_savings"; }
    std::string description() const override {
        return "30-day token-savings rollup (with-vs-without comparison). "
               "Aggregates per-layer telemetry: filter, compression, "
               "thinking-budget. Returns total tokens saved + estimated USD.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"window_days", "integer", "Window in days (default 30)", false},
        };
    }

protected:
    void validateArgs(const json& /*args*/) override {}

    json callImpl(const json& args, core::Db& /*db*/) override {
        int win = args.contains("window_days") && args["window_days"].is_number_integer()
                   ? args["window_days"].get<int>() : 30;
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        std::string cmd = "\"" + icmg + "\" savings --json --window "
                        + std::to_string(win) + "d";
        auto res = core::safeExecShell(cmd, false, 30000);
        try {
            auto j = json::parse(res.out);
            j["exit_code"] = res.exit_code;
            return j;
        } catch (...) {
            return {
                {"window_days", win},
                {"exit_code",   res.exit_code},
                {"stdout",      res.out},
                {"stderr",      res.err}
            };
        }
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_savings", SavingsTool);

} // namespace icmg::mcp
