#include "dispatcher.hpp"
#include "base_command.hpp"
#include "../core/registry.hpp"
#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>

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

    std::string cmd = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    // Check registry first (for later phases)
    auto& reg = icmg::core::Registry<icmg::BaseCommand>::instance();
    if (reg.has(cmd)) {
        auto handler = reg.create(cmd);
        return static_cast<BaseCommand*>(handler.get())->run(rest);
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
        "icmg 0.1.0 — unified memory, graph, and RTK tool\n\n"
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
    std::cout << "icmg 0.1.0\n";
}

} // namespace icmg::cli
