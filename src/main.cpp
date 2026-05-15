#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdio>
#include "core/config.hpp"
#include "core/db.hpp"
#include "core/migrator.hpp"
#include "core/logger.hpp"
#include "core/exec_utils.hpp"
#include "core/version_check.hpp"
#include "cli/dispatcher.hpp"
#include "mcp/server.hpp"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
// v0.58.1 popup fix: icmg.exe links as /SUBSYSTEM:WINDOWS (no auto-allocated
// console). When invoked from an existing console (cmd/bash/powershell), we
// attach to the parent's console so stdout/stderr/stdin work normally for
// interactive use. When invoked headless (Task Scheduler / Explorer / parent
// without console), AttachConsole returns false → nothing flashes.
static void attachParentConsoleIfAny() {
    // Snapshot whether parent passed real stdio handles (bash/mintty pipes,
    // shell redirects, etc.) BEFORE we touch the console.
    HANDLE h_out_pre = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE h_err_pre = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE h_in_pre  = GetStdHandle(STD_INPUT_HANDLE);
    bool inherited_out = (h_out_pre != nullptr && h_out_pre != INVALID_HANDLE_VALUE);
    bool inherited_err = (h_err_pre != nullptr && h_err_pre != INVALID_HANDLE_VALUE);
    bool inherited_in  = (h_in_pre  != nullptr && h_in_pre  != INVALID_HANDLE_VALUE);

    // Try to attach to parent's Windows console. Fails (returns FALSE) when:
    //   - parent has no console (Task Scheduler, Explorer, daemon ancestry) →
    //     icmg runs fully headless, no flash possible.
    //   - parent is bash/mintty/conpty (uses pseudo-tty pipes, not classic
    //     console) → AttachConsole fails too, but stdio pipes are still
    //     inherited so output reaches the tty fine via the pre handles.
    bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;

    // Rebind C stdio to CONOUT$/CONIN$ ONLY for the handles parent did NOT
    // already pass through. Otherwise we'd clobber working pipes (e.g. bash
    // redirecting icmg's stdout to a file).
    FILE* fp = nullptr;
    if (attached && !inherited_out) (void)freopen_s(&fp, "CONOUT$", "w", stdout);
    if (attached && !inherited_err) (void)freopen_s(&fp, "CONOUT$", "w", stderr);
    if (attached && !inherited_in)  (void)freopen_s(&fp, "CONIN$",  "r", stdin);
    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::cerr.clear();
    std::cin.clear();
}
#endif

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
#ifdef _WIN32
    attachParentConsoleIfAny();
#endif
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
        std::cout << "icmg 1.0.0\n";
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
        auto vstatus = icmg::core::checkVersionStaleness("1.0.0");
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
