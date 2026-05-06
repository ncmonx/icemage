#include <iostream>
#include <string>
#include <vector>
#include "core/config.hpp"
#include "core/db.hpp"
#include "core/migrator.hpp"
#include "core/logger.hpp"
#include "cli/dispatcher.hpp"

int main(int argc, char* argv[]) {
    // Parse global flags first
    std::vector<std::string> args;
    bool verbose = false;
    bool show_version = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--verbose" || a == "-v") {
            verbose = true;
        } else if (a == "--version") {
            show_version = true;
        } else {
            args.push_back(a);
        }
    }

    if (show_version) {
        std::cout << "icmg 0.1.0\n";
        return 0;
    }

    // Init config
    auto& cfg = icmg::core::Config::instance();
    cfg.load();
    if (verbose) cfg.setVerbose(true);

    // Init logger
    icmg::core::Logger::instance().init(cfg.logPath());

    // Auto-init project DB if needed
    try {
        std::string db_path = cfg.projectDbPath(".");
        icmg::core::ensureProjectDb(db_path);
    } catch (const std::exception& e) {
        std::cerr << "icmg: db init error: " << e.what() << "\n";
        return 1;
    }

    // Dispatch command
    icmg::cli::Dispatcher dispatcher;
    return dispatcher.run(args);
}
