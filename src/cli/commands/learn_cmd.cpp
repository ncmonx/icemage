// src/cli/commands/learn_cmd.cpp
// D6: `icmg learn` -- cross-session learning. Mines the accumulated per-command
// stats in the `commands` table (frequency + total_original/filtered lines,
// persisted across every session) and reports which commands are consistently
// noisy, recommending a tighter render mode (--nano / --gist).
//
// Read-only / advisory: it never changes behaviour, it surfaces what the data
// has learned so the user (or a future auto-router) can act on it.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../tkil/command_learn.hpp"
#include "../filter_gaps.hpp"
#include <iostream>
#include <vector>
#include <string>

namespace icmg::cli {

class LearnCommand : public BaseCommand {
public:
    std::string name()        const override { return "learn"; }
    std::string description() const override {
        return "Learn noisy commands from cross-session history; recommend --nano/--gist";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg learn [--json] [--limit N] [--min-freq N]\n\n"
            "  Mines the persistent `commands` table (frequency + output line\n"
            "  stats accumulated across sessions) and flags commands that are\n"
            "  consistently noisy -- frequently run, large output, most of which\n"
            "  is filtered away. For each, recommends a tighter render mode.\n\n"
            "  --json        Machine-readable output\n"
            "  --limit N     Show at most N recommendations (default 10)\n"
            "  --min-freq N  Minimum run count to trust a command (default 3)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (!args.empty() && (args[0] == "--help" || args[0] == "-h")) { usage(); return 0; }
        bool json_out = hasFlag(args, "--json");
        int limit = 10;
        if (std::string v = flagValue(args, "--limit"); !v.empty()) {
            try { limit = std::stoi(v); } catch (...) {}
        }
        tkil::LearnConfig cfg;
        if (std::string v = flagValue(args, "--min-freq"); !v.empty()) {
            try { cfg.min_frequency = std::stoll(v); } catch (...) {}
        }

        auto& c = core::Config::instance();
        core::Db db(c.projectDbPath("."));

        std::vector<tkil::CmdStat> stats;
        db.query(
            "SELECT command, frequency, total_original_lines, total_filtered_lines "
            "FROM commands WHERE frequency > 0",
            {},
            [&](const core::Row& r) {
                if (r.size() < 4) return;
                tkil::CmdStat s;
                s.command        = r[0];
                try { s.frequency      = std::stoll(r[1]); } catch (...) {}
                try { s.total_original = std::stoll(r[2]); } catch (...) {}
                try { s.total_filtered = std::stoll(r[3]); } catch (...) {}
                stats.push_back(std::move(s));
            });

        auto noisy = tkil::analyzeCommands(stats, cfg);
        if ((int)noisy.size() > limit) noisy.resize(limit);

        if (json_out) {
            std::cout << "{\"analyzed\":" << stats.size()
                      << ",\"noisy\":" << noisy.size() << ",\"recommendations\":[";
            for (size_t i = 0; i < noisy.size(); ++i) {
                const auto& n = noisy[i];
                if (i) std::cout << ",";
                std::cout << "{\"command\":\"";
                for (char ch : n.command) {   // minimal JSON escape
                    if (ch == '"' || ch == '\\') std::cout << '\\' << ch;
                    else if (ch == '\n') std::cout << "\\n";
                    else std::cout << ch;
                }
                std::cout << "\",\"avg_lines\":" << (int)n.avg_original
                          << ",\"filter_ratio\":" << n.filter_ratio
                          << ",\"recommend\":\"" << n.recommendation << "\"}";
            }
            std::cout << "]}\n";
            return 0;
        }

        if (stats.empty()) {
            std::cout << "[learn] no command history yet. Run some commands via "
                         "`icmg run <cmd>` first.\n";
            return 0;
        }
        if (noisy.empty()) {
            std::cout << "[learn] analyzed " << stats.size()
                      << " command(s); none are consistently noisy. Nothing to tune.\n";
            emitFilterGapSuggestions(db);
            return 0;
        }
        std::cout << "[learn] " << noisy.size() << " noisy command(s) from "
                  << stats.size() << " analyzed (cross-session):\n\n";
        for (const auto& n : noisy) {
            std::cout << "  " << n.command << "\n"
                      << "    avg " << (int)n.avg_original << " lines/run, "
                      << (int)((1.0 - n.filter_ratio) * 100) << "% filtered as noise\n"
                      << "    -> " << n.recommendation << "\n\n";
        }
        emitFilterGapSuggestions(db);
        return 0;
    }

 private:
    // 2026-07-16: turn the byte-level filter-coverage gaps that `icmg savings`
    // surfaces into an actionable recommendation right here in `icmg learn` --
    // "this verb wastes N KB at M% saved; register a filter like so". Closes
    // the loop from telemetry -> concrete next step, so a missing filter (the
    // gh api / git log class of bug) becomes a suggestion instead of waiting
    // to be re-discovered by hand.
    static void emitFilterGapSuggestions(core::Db& db) {
        auto gaps = findFilterGaps(db, /*since_ts=*/0, /*min_raw_bytes=*/50000,
                                   /*max_pct_saved=*/15.0, /*limit=*/3);
        if (gaps.empty()) return;
        std::cout << "\n[learn] filter-coverage gaps (verbs wasting the most, no/weak filter):\n\n";
        for (const auto& g : gaps) {
            std::cout << "  " << g.verb << "  (" << g.calls << " calls, "
                      << (g.raw_bytes / 1024) << " KB raw, "
                      << (int)(g.pct_saved + 0.5) << "% saved)\n"
                      << "    -> " << suggestFilterFor(g) << "\n\n";
        }
    }
};

ICMG_REGISTER_COMMAND("learn", LearnCommand);

} // namespace icmg::cli
