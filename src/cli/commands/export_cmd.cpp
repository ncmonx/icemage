#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../export/exporter.hpp"
#include <iostream>
#include <fstream>

namespace icmg::cli {

class ExportCommand : public BaseCommand {
public:
    std::string name()        const override { return "export"; }
    std::string description() const override { return "Export project data to JSON"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg export [options]\n\n"
            "Options:\n"
            "  --type <t>      Export subset: memory|graph|abbreviations|sp|rules\n"
            "  --output <f>    Write to file instead of stdout\n"
            "  --project <p>   Source project (uses current dir if not set)\n\n"
            "Examples:\n"
            "  icmg export > backup.json\n"
            "  icmg export --type memory > memory.json\n"
            "  icmg export --output backup.json\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }

        std::string type    = flagValue(args, "--type");
        std::string outFile = flagValue(args, "--output");

        auto& cfg = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        core::Db db(db_path);

        exporter::Exporter exp(db);

        if (!outFile.empty()) {
            std::ofstream f(outFile);
            if (!f) {
                std::cerr << "Cannot open output file: " << outFile << "\n";
                return 1;
            }
            exp.exportTo(f, type);
            std::cerr << "Exported to " << outFile << "\n";
        } else {
            exp.exportTo(std::cout, type);
        }

        return 0;
    }
};

ICMG_REGISTER_COMMAND("export", ExportCommand);

} // namespace icmg::cli
