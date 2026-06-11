// `icmg discipline` — per-session feature-coverage scorecard.
//
// Visibility-side enforcement: a tally of which discipline-critical icmg
// features the model touched this session (fed by the PostToolUse:Bash hook
// via `icmg discipline log <sub>`). Surfaces the "22 features, only ever use 3"
// blind spot as a number. Pure scoring lives in ../discipline_score.hpp
// (unit-tested); this adds the session ledger + presentation.
//
//   icmg discipline               Print the scorecard (used/total, %, cold list)
//   icmg discipline log <sub>     Record a used subcommand (hook; dedup set)
//   icmg discipline reset         Clear the session ledger (SessionStart hook)
//   icmg discipline inject        SessionStart additionalContext nudge if cold

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/rule_telemetry.hpp"
#include "../discipline_score.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

fs::path ledgerPath() { return fs::path(".icmg") / "feature-usage"; }

std::set<std::string> loadUsed() {
    std::set<std::string> u;
    std::ifstream f(ledgerPath());
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) u.insert(line);
    }
    return u;
}

void saveUsed(const std::set<std::string>& u) {
    std::error_code ec;
    fs::create_directories(ledgerPath().parent_path(), ec);
    std::ofstream f(ledgerPath());
    for (const auto& s : u) f << s << "\n";
}

}  // namespace

class DisciplineCommand : public BaseCommand {
public:
    std::string name()        const override { return "discipline"; }
    std::string description() const override {
        return "Per-session feature-coverage scorecard — which icmg features you actually used";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg discipline [action]\n\n"
            "  (no arg)        Print the scorecard (used/total, %, cold features)\n"
            "  log <sub>       Record a used subcommand (hook; deduped)\n"
            "  reset           Clear the session ledger (SessionStart)\n"
            "  report          Capstone dashboard: coverage + gate-firing telemetry\n"
            "  inject          SessionStart additionalContext nudge when coverage is low\n\n"
            "Core features tracked: recall pack context graph store wflog verify\n"
            "zone run parallel fail memoir. Fed by the PostToolUse:Bash hook.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args.empty() ? "show" : args[0];

        if (action == "log") {
            if (args.size() < 2) return 0;          // nothing to log; silent
            auto u = loadUsed();
            if (u.insert(args[1]).second) saveUsed(u);
            return 0;
        }

        if (action == "reset") {
            std::error_code ec;
            fs::remove(ledgerPath(), ec);
            std::cout << "icmg discipline: session ledger cleared\n";
            return 0;
        }

        DisciplineScore s = scoreDiscipline(loadUsed());

        if (action == "report") {
            // Capstone: closes the loop — enforce (ritual/recall-gate) AND
            // measure. Coverage + grade from the ledger; gate-firing counts
            // from the shared rule-viol telemetry (recorded by the gates when
            // they block/deny). Lets you SEE whether the model is improving:
            // fewer blocks over time = the discipline is landing.
            std::cout << "icmg discipline — session report\n"
                      << "  coverage: " << s.pct << "% (" << s.used << "/" << s.total
                      << ") — " << disciplineGrade(s.pct) << "\n";
            if (!s.cold.empty()) {
                std::cout << "  cold:     ";
                for (const auto& c : s.cold) std::cout << c << " ";
                std::cout << "\n";
            }
            // Gate-firing telemetry (the two positive-side hard gates).
            auto rows = core::RuleTelemetry::topByCount(50);
            bool anyGate = false;
            std::cout << "  gate firings (session / all-time):\n";
            for (const auto& r : rows) {
                if (r.rule_id != "ritual-block" && r.rule_id != "recall-gate-deny") continue;
                anyGate = true;
                std::cout << "    " << r.rule_id << "  "
                          << r.count_session << " / " << r.count_total << "\n";
            }
            if (!anyGate)
                std::cout << "    (none yet — gates clean this session)\n";
            return 0;
        }

        if (action == "inject") {
            // Only nudge when coverage is meaningfully low (avoids noise once
            // the session is warmed up). Emits the SessionStart line format.
            if (s.pct >= 50) return 0;
            std::cout << "[icmg] feature coverage " << s.pct << "% this session ("
                      << s.used << "/" << s.total << "). Cold: ";
            for (size_t i = 0; i < s.cold.size() && i < 6; ++i)
                std::cout << s.cold[i] << (i + 1 < s.cold.size() && i < 5 ? " " : "");
            std::cout << " — reach for them when they fit.\n";
            return 0;
        }

        // Default: human-readable scorecard.
        std::cout << "icmg discipline — feature coverage this session\n"
                  << "  score: " << s.pct << "%  (" << s.used << "/" << s.total
                  << " core features used)\n";
        if (!s.cold.empty()) {
            std::cout << "  cold:  ";
            for (const auto& c : s.cold) std::cout << c << " ";
            std::cout << "\n";
        } else {
            std::cout << "  all core features used — clean session.\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("discipline", DisciplineCommand);

}  // namespace icmg::cli
