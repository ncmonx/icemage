#include <iostream>
#include <string>
#include <vector>
#include "core/config.hpp"
#include "core/db.hpp"
#include "core/migrator.hpp"
#include "core/logger.hpp"
#include "cli/dispatcher.hpp"
#include "mcp/server.hpp"

int main(int argc, char* argv[]) {
    // Parse global flags first
    std::vector<std::string> args;
    bool verbose = false;
    bool show_version = false;
    bool mcp_server = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--verbose" || a == "-v") {
            verbose = true;
        } else if (a == "--version") {
            show_version = true;
        } else if (a == "--mcp-server") {
            mcp_server = true;
        } else {
            args.push_back(a);
        }
    }

    if (show_version) {
        std::cout << "icmg 0.1.2\n";
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

    // Dispatch command
    icmg::cli::Dispatcher dispatcher;
    return dispatcher.run(args);
}
