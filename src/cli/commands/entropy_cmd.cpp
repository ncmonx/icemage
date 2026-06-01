// Phase 67 T10: `icmg entropy` — file-mtime variance heatmap.
//
// High-variance files change frequently → likely stale fast → poor pack
// candidates. Score via git log: count commits in last 90d per file. Top
// files demoted in pack ranking by `pack --entropy-demote` flag.
//
// Cheap implementation: shells out to `git log --name-only --since=90d`
// once, counts occurrences per file, stores in `entropy_score` table.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace icmg::cli {

class EntropyCommand : public BaseCommand {
public:
    std::string name()        const override { return "entropy"; }
    std::string description() const override {
        return "Compute file-edit entropy from git history (high = stale-fast = bad pack candidate)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg entropy <action> [options]\n\n"
            "Actions:\n"
            "  scan [--days N]    Re-scan git log; populate entropy_score table\n"
            "  show [--top N]     Show highest-entropy files\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        // Ensure table.
        db.run("CREATE TABLE IF NOT EXISTS entropy_score ("
               "  path        TEXT PRIMARY KEY,"
               "  commits     INTEGER NOT NULL,"
               "  scanned_at  INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
               ")");

        if (action == "scan") {
            int days = 90;
            try { days = std::stoi(flagValue(args, "--days", "90")); } catch (...) {}
            std::string cmd = "git log --name-only --since=" + std::to_string(days)
                            + ".days --pretty=format: 2>/dev/null";
            auto res = core::safeExec({"sh", "-c", cmd}, true, 30000);
            if (res.exit_code != 0) {
                std::cerr << "icmg entropy: git log failed (not a git repo?)\n";
                return 1;
            }
            std::map<std::string, int> counts;
            std::istringstream iss(res.out);
            std::string line;
            while (std::getline(iss, line)) {
                while (!line.empty() && (line.back() == '\r')) line.pop_back();
                if (line.empty()) continue;
                ++counts[line];
            }
            db.run("DELETE FROM entropy_score");
            for (auto& [p, c] : counts) {
                db.run("INSERT INTO entropy_score (path, commits) VALUES (?, ?)",
                       {p, std::to_string(c)});
            }
            std::cout << "icmg entropy: scanned " << counts.size()
                      << " file(s) over " << days << "d window.\n";
            return 0;
        }

        if (action == "show") {
            int top = 20;
            try { top = std::stoi(flagValue(args, "--top", "20")); } catch (...) {}
            std::cout << "Top " << top << " hottest files (most commits):\n";
            std::cout << "  " << std::left
                      << std::setw(8)  << "commits"
                      << "path\n";
            std::cout << "  " << std::string(60, '-') << "\n";
            db.query(
                "SELECT path, commits FROM entropy_score"
                " ORDER BY commits DESC LIMIT ?",
                {std::to_string(top)},
                [](const core::Row& r) {
                    if (r.size() < 2) return;
                    std::cout << "  " << std::left << std::setw(8) << r[1] << r[0] << "\n";
                });
            return 0;
        }

        std::cerr << "icmg entropy: unknown action '" << action << "'\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("entropy", EntropyCommand);

} // namespace icmg::cli
