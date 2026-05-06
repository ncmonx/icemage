#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../rtk/rtk.hpp"
#include <iostream>
#include <sstream>

namespace icmg::cli {

class RunCommand : public BaseCommand {
public:
    std::string name()        const override { return "run"; }
    std::string description() const override { return "Run command with smart output filter"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg run [options] <command...>\n\n"
            "Options:\n"
            "  --raw        No filtering — show raw output\n"
            "  --json       JSON output with metadata\n"
            "  --dry-run    Show what would be filtered without executing\n"
            "  --stream     Streaming filter (real-time output)\n"
            "  --timeout N  Execution timeout in ms (default: 60000)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        bool raw      = hasFlag(args, "--raw");
        bool json_out = hasFlag(args, "--json");
        bool dry_run  = hasFlag(args, "--dry-run");
        bool stream   = hasFlag(args, "--stream");

        // Everything that's not a flag is part of the command
        std::string command;
        for (auto& a : args) {
            if (a.empty()) continue;
            if (a[0] == '-' && (a == "--raw" || a == "--json" ||
                                a == "--dry-run" || a == "--stream")) continue;
            if (!command.empty()) command += " ";
            command += a;
        }
        if (command.empty()) { std::cerr << "icmg run: requires <command>\n"; return 1; }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        rtk::RTK executor(db);

        return executor.runFiltered(command, raw, json_out, dry_run, stream);
    }
};

ICMG_REGISTER_COMMAND("run", RunCommand);

} // namespace icmg::cli
