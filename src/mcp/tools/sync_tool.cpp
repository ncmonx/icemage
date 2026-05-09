// Phase 50 T4: icmg_sync MCP tool — exposes team sync (push/pull/status).
// Wraps icmg sync subcommands so AI agents can share/fetch memory + graph
// from teammate snapshots without shellout.

#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <cstdlib>
#include <filesystem>

namespace icmg::mcp {

class SyncTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_sync"; }
    std::string description() const override {
        return "Team sync: push/pull memory + graph via git-tracked JSONL. "
               "Use 'push' to share local decisions, 'pull' to fetch teammate "
               "updates, 'status' for snapshot row counts.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"action",       "string",  "init|push|pull|status",      true},
            {"force_remote", "boolean", "On pull conflict, take remote", false},
            {"dry_run",      "boolean", "Preview without DB writes",    false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        std::string a = getStr(args, "action");
        if (a != "init" && a != "push" && a != "pull" && a != "status") {
            throw McpError(-32602, "action must be init|push|pull|status");
        }
    }

    json callImpl(const json& args, core::Db& /*db*/) override {
        std::string action = getStr(args, "action");
        bool force_remote  = args.contains("force_remote") && args["force_remote"].is_boolean()
                              ? args["force_remote"].get<bool>() : false;
        bool dry_run       = args.contains("dry_run") && args["dry_run"].is_boolean()
                              ? args["dry_run"].get<bool>() : false;

        // Locate icmg binary (assume on PATH for MCP-launched subprocess; else
        // fall back to env hint).
        std::string icmg = locateIcmg();
        if (icmg.empty()) {
            return {{"error", "icmg binary not found on PATH; cannot subshell"}};
        }
        std::string cmd = "\"" + icmg + "\" sync " + action;
        if (force_remote) cmd += " --force-remote";
        if (dry_run)      cmd += " --dry-run";
        auto res = core::safeExecShell(cmd, false, 60000);
        return {
            {"action",     action},
            {"exit_code",  res.exit_code},
            {"stdout",     res.out},
            {"stderr",     res.err}
        };
    }

private:
    static std::string locateIcmg() {
        // Try $ICMG_BIN env first, then fallback "icmg".
        const char* env = std::getenv("ICMG_BIN");
        if (env && *env && std::filesystem::exists(env)) return env;
        return "icmg";
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_sync", SyncTool);

} // namespace icmg::mcp
