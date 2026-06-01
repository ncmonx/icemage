// v1.25.0 (W5): icmg_write_mode MCP tool — read-only status probe.
// Toggle is deliberate user gesture (not AI auto-fire) — MCP only exposes
// status query.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>

namespace icmg::mcp {

class WriteModeTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_write_mode"; }
    std::string description() const override {
        return "Check whether compressed-write protocol is enabled (v1.25.0 W2). "
               "Returns current mode or 'off'. Toggle via `icmg write-mode on/off` "
               "CLI — not exposed as MCP to keep the toggle a deliberate user gesture.";
    }
    std::vector<McpToolParam> params() const override { return {}; }

protected:
    json callImpl(const json& /*args*/, core::Db& /*db*/) override {
        std::string icmg = std::getenv("ICMG_BIN") ? std::getenv("ICMG_BIN") : "icmg";
        auto res = core::safeExecShell("\"" + icmg + "\" write-mode status",
                                        false, 5000);
        return {{"exit_code", res.exit_code}, {"stdout", res.out}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_write_mode", WriteModeTool);

}  // namespace icmg::mcp
