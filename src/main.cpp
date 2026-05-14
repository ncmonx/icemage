#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "core/config.hpp"
#include "core/db.hpp"
#include "core/migrator.hpp"
#include "core/logger.hpp"
#include "core/exec_utils.hpp"
#include "core/version_check.hpp"
#include "cli/dispatcher.hpp"
#include "mcp/server.hpp"

namespace fs = std::filesystem;

// Auto-install hooks when icmg first enters a project dir (.claude/ or .git/
// present but .icmg/ not yet created). Skipped for `init` and `update` to
// avoid recursion / interference with those commands.
static void autoBootstrapProject(const std::vector<std::string>& args) {
    if (!args.empty() && (args[0] == "init" || args[0] == "update")) return;
    auto cwd = fs::current_path();
    if (fs::exists(cwd / ".icmg")) return;
    if (!fs::exists(cwd / ".claude") && !fs::exists(cwd / ".git")) return;
    icmg::core::safeExecShell(
        "icmg init --install-hooks --force --no-agents --no-embedder "
        "--no-scan --no-backup --no-maintain --no-mirror "
        "--no-sentinel --no-auto-upgrade",
        false, 25000);
}

int main(int argc, char* argv[]) {
    // Parse global flags first
    std::vector<std::string> args;
    bool verbose = false;
    bool show_version = false;
    bool mcp_server = false;

    // Stop global-flag parsing once we hit the first subcommand. Otherwise
    // commands like `icmg run node --version` would eat --version globally
    // and never reach the subprocess.
    bool seen_subcommand = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (!seen_subcommand) {
            if (a == "--verbose" || a == "-v") { verbose = true; continue; }
            if (a == "--version")              { show_version = true; continue; }
            if (a == "--mcp-server")           { mcp_server = true; continue; }
            if (!a.empty() && a[0] != '-')     seen_subcommand = true;
        }
        args.push_back(a);
    }

    if (show_version) {
        std::cout << "icmg 0.56.0\n";
        return 0;
    }

    // Init config
    auto& cfg = icmg::core::Config::instance();
    cfg.load();
    if (verbose) cfg.setVerbose(true);

    // Init logger
    icmg::core::Logger::instance().init(cfg.logPath());

    // Auto-init project DB if needed
    std::string db_path;
    try {
        db_path = cfg.projectDbPath(".");
        icmg::core::ensureProjectDb(db_path);
    } catch (const std::exception& e) {
        std::cerr << "icmg: db init error: " << e.what() << "\n";
        return 1;
    }

    // MCP server mode — run stdio JSON-RPC loop
    if (mcp_server) {
        try {
            icmg::core::Db db(db_path);
            icmg::mcp::McpServer server(db);
            server.run();
        } catch (const std::exception& e) {
            std::cerr << "icmg: mcp server error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    // Auto-bootstrap project hooks on first entry (silent, non-blocking).
    autoBootstrapProject(args);

    // Version staleness check (cached — no network hit when cache is fresh).
    // Skip for MCP mode, version queries, and update commands.
    if (!args.empty() && args[0] != "update" && args[0] != "upgrade") {
        auto vstatus = icmg::core::checkVersionStaleness("0.56.0");
        icmg::core::printVersionWarning(vstatus);
        if (!args.empty() && icmg::core::isCommandSoftBlocked(args[0], vstatus)) {
            std::cerr << "[icmg] Command '" << args[0]
                      << "' is disabled on this stale version. Run: icmg upgrade\n";
            return 1;
        }
    }

    // Dispatch command
    icmg::cli::Dispatcher dispatcher;
    return dispatcher.run(args);
}
