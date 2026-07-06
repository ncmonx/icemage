// `icmg skill-bank` -- distill successful command trajectories into a bounded,
// reusable skill bank (feature #7). Deterministic: reads the `verifications`
// table (command + exit_code history), aggregates per-command success/uses,
// and ranks proven-successful commands with causal attribution (success rate).
// No LLM. Pure ranking lives in imem/skill_bank.hpp (unit-tested).
//
// Extends `icmg learn` (raw frequency) + the workflow verifications audit trail
// rather than parallel-building a new mechanism (anti-dup).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/skill_bank.hpp"

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace icmg::cli {

class SkillBankCommand : public BaseCommand {
public:
    std::string name()        const override { return "skill-bank"; }
    std::string description() const override {
        return "Distill proven-successful command trajectories into a bounded skill bank";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg skill-bank [--max N] [--min-freq N] [--min-rate R] [--json]\n"
            "  Rank commands that repeatedly SUCCEEDED (verifications exit_code=0)\n"
            "  into a bounded skill bank with success-rate attribution.\n"
            "  --max N       cap the bank size (default 20)\n"
            "  --min-freq N  ignore commands used < N times (default 2)\n"
            "  --min-rate R  ignore success rate < R, 0..1 (default 0.5)\n"
            "  --json        machine-readable output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        int    maxN    = intFlag(args, "--max", 20);
        int    minFreq = intFlag(args, "--min-freq", 2);
        double minRate = 0.5;
        { std::string r = flagValue(args, "--min-rate");
          if (!r.empty()) { try { minRate = std::stod(r); } catch (...) {} } }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        // Aggregate per-command uses + successes from the verifications trail.
        std::unordered_map<std::string, imem::TrajectoryStat> agg;
        db.query("SELECT command, exit_code FROM verifications", {},
                 [&](const core::Row& r) {
                     if (r.size() < 2) return;
                     auto& t = agg[r[0]];
                     t.command = r[0];
                     ++t.uses;
                     int rc = 0; try { rc = std::stoi(r[1]); } catch (...) { rc = 1; }
                     if (rc == 0) ++t.successes;
                 });

        std::vector<imem::TrajectoryStat> stats;
        stats.reserve(agg.size());
        for (auto& kv : agg) stats.push_back(kv.second);

        auto skills = imem::distillSkills(stats, maxN, minFreq, minRate);

        bool json = hasFlag(args, "--json");
        if (json) {
            std::cout << "[";
            for (size_t i = 0; i < skills.size(); ++i) {
                const auto& s = skills[i];
                std::cout << (i ? "," : "")
                          << "{\"command\":\"" << jsonEscape(s.command)
                          << "\",\"uses\":" << s.uses
                          << ",\"success_rate\":" << s.success_rate << "}";
            }
            std::cout << "]\n";
            return 0;
        }

        if (skills.empty()) {
            std::cout << "skill-bank: no proven trajectories yet "
                         "(record some via `icmg verify --command ...`).\n";
            return 0;
        }
        std::cout << "Skill bank (" << skills.size() << " proven, bounded to " << maxN << "):\n";
        for (const auto& s : skills) {
            std::cout << "  [" << (int)(s.success_rate * 100) << "% x" << s.uses << "]  "
                      << s.command << "\n";
        }
        return 0;
    }

private:
    static int intFlag(const std::vector<std::string>& args, const std::string& f, int def) {
        std::string v = flagValue(args, f);
        if (v.empty()) return def;
        try { return std::stoi(v); } catch (...) { return def; }
    }
    static std::string jsonEscape(const std::string& s) {
        std::string o; o.reserve(s.size());
        for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
        return o;
    }
};

ICMG_REGISTER_COMMAND("skill-bank", SkillBankCommand);

} // namespace icmg::cli
