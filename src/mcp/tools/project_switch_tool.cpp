#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"

namespace icmg::mcp {

class ProjectSwitchTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_project_switch"; }
    std::string description() const override {
        return "Switch the active project context for subsequent MCP tool calls.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"project_name", "string", "Project root path", true},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "project_name", 500);
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string project = getStr(args, "project_name");

        // Compute the project DB path and set as override
        auto& cfg = core::Config::instance();
        std::string db_path = cfg.projectDbPath(project);
        cfg.setProjectDbOverride(db_path);

        return {
            {"status",   "switched"},
            {"project",  project},
            {"db_path",  db_path},
            {"note",     "Subsequent calls will use this project's DB"}
        };
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_project_switch", ProjectSwitchTool);

} // namespace icmg::mcp
