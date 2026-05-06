#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../import/base_importer.hpp"
#include <iostream>
#include <chrono>

namespace icmg::cli {

class ImportCommand : public BaseCommand {
public:
    std::string name()        const override { return "import"; }
    std::string description() const override { return "Import data from external sources"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg import <format> <source> [options]\n\n"
            "Formats:\n"
            "  icm <path>         Import from ICM MCP tool SQLite database\n"
            "  graphify <path>    Import from Graphify GRAPH_REPORT.md\n"
            "  json <file>        Import from icmg JSON export\n"
            "  csv <file>         Import from CSV file\n\n"
            "Options:\n"
            "  --type <t>         CSV type: abbreviation | memory\n"
            "  --project <name>   Target project context\n"
            "  --list             List available importers\n\n"
            "Examples:\n"
            "  icmg import icm ~/.icm/memory.db\n"
            "  icmg import graphify ./graphify-out/GRAPH_REPORT.md\n"
            "  icmg import json backup.json\n"
            "  icmg import csv abbr.csv --type abbreviation\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        if (args[0] == "--list") {
            auto keys = core::Registry<BaseImporter>::instance().keys();
            std::cout << "Available importers:\n";
            for (auto& k : keys) {
                auto imp = core::Registry<BaseImporter>::instance().create(k);
                std::cout << "  " << k << "  - " << imp->description() << "\n";
            }
            return 0;
        }

        std::string format = args[0];
        if (args.size() < 2) {
            std::cerr << "icmg import: missing source path\n";
            usage(); return 1;
        }
        std::string source = args[1];

        std::string typeHint   = flagValue(args, "--type");
        std::string projName   = flagValue(args, "--project");

        // For CSV importer: pass type hint via project_name
        std::string contextHint = typeHint.empty() ? projName : typeHint;

        // Get target DB
        auto& cfg = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        core::Db db(db_path);

        // Get importer
        if (!core::Registry<BaseImporter>::instance().has(format)) {
            std::cerr << "icmg import: unknown format '" << format << "'\n";
            std::cerr << "Run 'icmg import --list' to see available importers\n";
            return 1;
        }

        auto imp = core::Registry<BaseImporter>::instance().create(format);

        std::cout << "Import from " << format << ": " << source << "\n";

        auto t0 = std::chrono::steady_clock::now();

        ImportStats stats;
        try {
            stats = imp->import(source, db, contextHint);
        } catch (const ImportError& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        } catch (const ValidationError& e) {
            std::cerr << "Validation error: " << e.what() << "\n";
            return 1;
        } catch (const std::exception& e) {
            std::cerr << "Unexpected error: " << e.what() << "\n";
            return 1;
        }

        auto t1  = std::chrono::steady_clock::now();
        int ms   = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        // Summary
        if (stats.memory_nodes)  std::cout << "  Memory nodes:      " << stats.memory_nodes  << " imported\n";
        if (stats.graph_nodes)   std::cout << "  Graph nodes:       " << stats.graph_nodes   << " imported\n";
        if (stats.graph_edges)   std::cout << "  Graph edges:       " << stats.graph_edges   << " imported\n";
        if (stats.abbreviations) std::cout << "  Abbreviations:     " << stats.abbreviations << " imported\n";
        if (stats.stored_procs)  std::cout << "  Stored procedures: " << stats.stored_procs  << " imported\n";
        if (stats.rules)         std::cout << "  Rules:             " << stats.rules         << " imported\n";
        if (stats.errors)        std::cout << "  Errors:            " << stats.errors        << "\n";

        for (auto& msg : stats.error_messages) {
            std::cout << "    ! " << msg << "\n";
        }

        std::cout << "  Duration: " << ms << "ms\n";
        std::cout << "Done.\n";
        return stats.errors > 0 ? 1 : 0;
    }
};

ICMG_REGISTER_COMMAND("import", ImportCommand);

} // namespace icmg::cli
