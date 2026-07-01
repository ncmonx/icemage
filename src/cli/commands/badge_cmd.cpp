// `icmg badge` -- thin formatter that turns existing `savings` data into a
// shields.io endpoint-JSON badge printed to stdout.
//
// Data is sourced by spawning `icmg savings --json --window-days N` and parsing
// the result (subprocess), to keep zero risk to the existing 1161-line `savings`
// command -- no refactor of savings_cmd.cpp in v1. All formatting lives in
// badge_core.hpp (pure, unit-tested). This file is only argv glue + subprocess.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/path_utils.hpp"
#include "../badge_core.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class BadgeCommand : public BaseCommand {
public:
    std::string name() const override { return "badge"; }
    std::string description() const override {
        return "Generate a shields.io endpoint-JSON token-savings badge (stdout)";
    }
    void usage() const override {
        std::cout <<
            "Usage: icmg badge [--metric savings|tokens|cost] [--window-days N]\n\n"
            "Prints shields.io endpoint JSON to stdout. Pipe to a gist + embed:\n"
            "  ![](https://img.shields.io/endpoint?url=<raw-gist-url>)\n\n"
            "Metrics:\n"
            "  savings  token-savings percentage (default)\n"
            "  tokens   humanized tokens saved (e.g. 10.4M)\n"
            "  cost     dollars saved (e.g. $23.2)\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        std::string metric = "savings";
        std::string windowDays = "30";
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--metric" && i + 1 < args.size())           metric     = args[++i];
            else if (args[i] == "--window-days" && i + 1 < args.size()) windowDays = args[++i];
        }
        // Guard: windowDays must be strictly numeric (safety + clean value).
        // Reject anything else -> default 30.
        if (windowDays.empty() ||
            windowDays.find_first_not_of("0123456789") != std::string::npos) {
            windowDays = "30";
        }
        // Spawn the SAME binary (selfExePath, not bare "icmg" on PATH -> no
        // PATH-spoof / not-installed failure) via argv form (no shell -> no
        // injection). savingsArgv translates --window-days -> savings' --window.
        std::string exe = icmg::core::selfExePath();
        if (exe.empty()) exe = "icmg";  // fallback: PATH lookup
        auto res = icmg::core::safeExec(savingsArgv(exe, windowDays));
        BadgeData d = parseSavingsJson(res.out);
        std::cout << renderBadge(metric, d) << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("badge", BadgeCommand);

}  // namespace icmg::cli
