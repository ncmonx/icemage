#include "dispatcher.hpp"
#include "base_command.hpp"
#include "../core/registry.hpp"
#include "../core/config.hpp"
#include "../core/global_db.hpp"
#include "../core/project_context.hpp"
#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>

// Pull in all registered commands (each file registers via ICMG_REGISTER_COMMAND)
// The linker needs to see these TUs to trigger static-init registration.
// We rely on GLOB_RECURSE in CMakeLists.txt to include all src/*.cpp files.

namespace icmg::cli {

// Inline stub commands for phase-01 skeleton.
// Real implementations registered via ICMG_REGISTER_COMMAND in later phases.

struct StubCommand : BaseCommand {
    std::string name_;
    std::string desc_;
    StubCommand(std::string n, std::string d)
        : name_(std::move(n)), desc_(std::move(d)) {}
    std::string name()        const override { return name_; }
    std::string description() const override { return desc_; }
    int run(const std::vector<std::string>&) override {
        std::cout << name_ << ": not yet implemented (planned)\n";
        return 0;
    }
};

// Command table: name → description
static const std::vector<std::pair<std::string,std::string>> CMDS = {
    {"store",    "Store a memory node"},
    {"recall",   "Recall memory nodes by query"},
    {"graph",    "Manage knowledge graph (scan, query, visualize)"},
    {"run",      "Run command through RTK filter"},
    {"sp",       "Stored procedure management"},
    {"abbr",     "Abbreviation management"},
    {"rule",     "Per-folder rule management"},
    {"data",     "Structured data (model/view/behavior/schema)"},
    {"project",  "Multi-project registry"},
    {"cmd",      "Command frequency tracking"},
    {"stats",    "Show usage statistics"},
    {"import",   "Import from ICM/Graphify/JSON/CSV"},
    {"export",   "Export data"},
    {"doctor",   "Health check (DB integrity, schema version, config)"},
};

Dispatcher::Dispatcher() {}

int Dispatcher::run(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        printHelp();
        return 0;
    }

    // Parse --project <name> from args (can appear anywhere before command)
    std::string project_flag;
    std::vector<std::string> cleaned;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--project" && i + 1 < args.size()) {
            project_flag = args[++i];
        } else {
            cleaned.push_back(args[i]);
        }
    }

    // Resolve project context + set override on Config
    if (!project_flag.empty()) {
        try {
            auto ctx = core::ProjectContext::resolve(project_flag);
            // A2: cross-project warning
            std::cerr << "⚠  Cross-project read: " << ctx.name()
                      << " (" << ctx.rootPath() << ")\n"
                      << "   No authentication — any local user can read this data.\n";
            core::Config::instance().setProjectDbOverride(ctx.dbPath());
        } catch (const std::exception& e) {
            std::cerr << "icmg: " << e.what() << "\n";
            return 1;
        }
    }

    std::string cmd = cleaned.empty() ? "" : cleaned[0];
    if (cmd.empty()) { printHelp(); return 0; }
    std::vector<std::string> rest(cleaned.begin() + 1, cleaned.end());

    // Check registry first (real implementations from later phases)
    auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
    if (reg.has(cmd)) {
        auto handler = reg.create(cmd);
        return handler->run(rest);
    }

    // Try compound command: "graph scan" → look up "graph-scan" in registry
    if (!rest.empty()) {
        std::string compound = cmd + "-" + rest[0];
        if (reg.has(compound)) {
            auto handler = reg.create(compound);
            return handler->run(std::vector<std::string>(rest.begin() + 1, rest.end()));
        }
    }

    // Fall back to stubs
    for (auto& [name, desc] : CMDS) {
        if (name == cmd) {
            StubCommand stub(name, desc);
            return stub.run(rest);
        }
    }

    std::cerr << "icmg: unknown command: " << cmd << "\n";
    std::cerr << "Run 'icmg --help' for usage.\n";
    return 1;
}

void Dispatcher::printHelp() const {
    std::cout <<
        "icmg 0.1.6 — unified memory, graph, and RTK tool\n\n"
        "Usage: icmg <command> [options]\n\n"
        "Commands:\n";
    for (auto& [name, desc] : CMDS) {
        std::cout << "  " << std::left;
        std::cout.width(12);
        std::cout << name << desc << "\n";
    }
    std::cout <<
        "\nGlobal flags:\n"
        "  --verbose, -v   Verbose output\n"
        "  --version       Show version\n"
        "  --help, -h      Show this help\n";
}

void Dispatcher::printVersion() const {
    std::cout << "icmg 0.1.6\n";
}

} // namespace icmg::cli
