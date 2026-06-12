// `icmg ritual` — post-change 5-sync gate (POSITIVE-side enforcement).
//
// Pairs with the defensive `strict`/`rule-eval` enforcement (which blocks
// NATIVE calls). This enforces the OTHER half of the rule: after a code change
// you MUST run the sync ritual (graph update / store / wflog ; zone + verify
// advisory). The pure verdict logic lives in ../ritual_gate.hpp (unit-tested);
// this command adds the per-project state file + the Stop-hook gate JSON.
//
//   icmg ritual touch                  Mark that a code change happened (hook:
//                                       PostToolUse on Edit/Write).
//   icmg ritual saw <sub> [arg1...]    Feed an invoked icmg command; if it is a
//                                       sync step, record it. Auto-clears the
//                                       owed state once all required steps seen
//                                       (hook: PostToolUse on Bash).
//   icmg ritual saw-line "<full cmd>"  Like `saw` but parses the WHOLE command
//                                       line: strips VAR= prefixes + scans every
//                                       &&/||/;/| segment. The PostToolUse:Bash
//                                       hook uses this so `RAW=1 icmg store` and
//                                       `icmg store && icmg wflog` both record.
//   icmg ritual did <step>             Manually mark a step done (graph|store|
//                                       zone|wflog|verify).
//   icmg ritual status                 Human-readable state + missing steps.
//   icmg ritual check                  Exit 0 = clean/satisfied, 2 = owed.
//   icmg ritual gate                   If owed, print the Stop-hook block JSON
//                                       (decision:block); else nothing. Opt out
//                                       with ICMG_NO_RITUAL_GATE=1.
//   icmg ritual clear                  Reset state (ritual completed/abandoned).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/rule_telemetry.hpp"
#include "../ritual_gate.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

fs::path statePath() { return fs::path(".icmg") / "ritual.state"; }

struct DiskState {
    int changedCount = 0;             // # edits since last clear (0 = not changed)
    std::set<RitualStep> done;
};

std::optional<RitualStep> parseStep(const std::string& name) {
    if (name == "graph-update" || name == "graph") return RitualStep::Graph;
    if (name == "store")  return RitualStep::Store;
    if (name == "zone")   return RitualStep::Zone;
    if (name == "wflog")  return RitualStep::Wflog;
    if (name == "verify") return RitualStep::Verify;
    return std::nullopt;
}

DiskState load() {
    DiskState d;
    std::ifstream f(statePath());
    if (!f) return d;
    std::string line;
    if (std::getline(f, line)) { try { d.changedCount = std::stoi(line); } catch (...) {} }
    if (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tok;
        while (ss >> tok) if (auto st = parseStep(tok)) d.done.insert(*st);
    }
    return d;
}

void save(const DiskState& d) {
    std::error_code ec;
    fs::create_directories(statePath().parent_path(), ec);
    std::ofstream f(statePath());
    f << d.changedCount << "\n";
    bool first = true;
    for (RitualStep s : d.done) { f << (first ? "" : " ") << ritualStepName(s); first = false; }
    f << "\n";
}

RitualState toCore(const DiskState& d) {
    RitualState s;
    s.changed = d.changedCount > 0;
    s.done = d.done;
    return s;
}

}  // namespace

class RitualCommand : public BaseCommand {
public:
    std::string name()        const override { return "ritual"; }
    std::string description() const override {
        return "Post-change 5-sync gate — enforce graph/store/wflog after a code change";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg ritual <action>\n\n"
            "  touch                 Mark a code change happened (Edit/Write hook)\n"
            "  saw <sub> [arg1]      Feed an invoked icmg cmd; record sync step; auto-clear\n"
            "  did <step>            Mark a step done (graph|store|zone|wflog|verify)\n"
            "  status                Show state + missing required steps\n"
            "  check                 Exit 0 = satisfied, 2 = owed (for scripting)\n"
            "  gate                  If owed, print Stop-hook block JSON (else nothing)\n"
            "  clear                 Reset state (ritual completed)\n\n"
            "Required after a change: store + wflog (the judgment steps). Graph auto-\n"
            "scans at turn end; graph/zone/verify are advisory (tracked, never block).\n"
            "Opt out of the gate: ICMG_NO_RITUAL_GATE=1.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string action = args[0];

        if (action == "touch") {
            DiskState d = load();
            d.changedCount += 1;
            save(d);
            std::cout << "icmg ritual: change recorded (" << d.changedCount
                      << " since last sync) — owe store + wflog\n";
            return 0;
        }

        if (action == "saw") {
            if (args.size() < 2) return 0;             // nothing to record; silent
            std::string sub  = args[1];
            std::string arg1 = args.size() > 2 ? args[2] : "";
            auto step = stepForCommand(sub, arg1);
            if (!step) return 0;                       // ritual-neutral; silent
            DiskState d = load();
            d.done.insert(*step);
            // Auto-clear once the required set is satisfied.
            if (!evaluateRitual(toCore(d)).owed && d.changedCount > 0) {
                d = DiskState{};                       // reset: ritual complete
                save(d);
                std::cout << "icmg ritual: 5-sync complete — gate cleared\n";
            } else {
                save(d);
            }
            return 0;
        }

        if (action == "saw-line") {
            // Robust recorder (known-issue #33370): given the FULL command line,
            // detect every icmg sync sub-command even behind a VAR= env prefix or
            // joined by &&/||/;/|. Replaces the fragile single-token `saw` parse
            // in the Bash hook (which missed `RAW=1 icmg store` and chained cmds).
            if (args.size() < 2) return 0;
            auto steps = parseSyncStepsFromCommandLine(args[1]);
            if (steps.empty()) return 0;
            DiskState d = load();
            for (RitualStep s : steps) d.done.insert(s);
            if (!evaluateRitual(toCore(d)).owed && d.changedCount > 0) {
                d = DiskState{};
                save(d);
                std::cout << "icmg ritual: 5-sync complete — gate cleared\n";
            } else {
                save(d);
            }
            return 0;
        }

        if (action == "did") {
            if (args.size() < 2) { std::cerr << "did requires a step name\n"; return 2; }
            auto step = parseStep(args[1]);
            if (!step) { std::cerr << "unknown step '" << args[1] << "'\n"; return 2; }
            DiskState d = load();
            d.done.insert(*step);
            if (!evaluateRitual(toCore(d)).owed && d.changedCount > 0) {
                d = DiskState{}; save(d);
                std::cout << "icmg ritual: 5-sync complete — gate cleared\n";
            } else { save(d); }
            return 0;
        }

        if (action == "clear") {
            DiskState d{};
            save(d);
            std::cout << "icmg ritual: state cleared\n";
            return 0;
        }

        DiskState d = load();
        RitualVerdict v = evaluateRitual(toCore(d));

        if (action == "status") {
            std::cout << "icmg ritual: "
                      << (d.changedCount == 0 ? "no pending change"
                          : (v.owed ? "OWED" : "satisfied"))
                      << " (" << d.changedCount << " edit(s) since last sync)\n";
            if (!d.done.empty()) {
                std::cout << "  done:    ";
                for (RitualStep s : d.done) std::cout << ritualStepName(s) << " ";
                std::cout << "\n";
            }
            if (v.owed) {
                std::cout << "  missing: ";
                for (RitualStep s : v.missing) std::cout << ritualStepName(s) << " ";
                std::cout << "\n  run: icmg store --topic decisions-... \"...\" && icmg wflog save --goal \"...\"\n";
            }
            return 0;
        }

        if (action == "check") {
            return v.owed ? 2 : 0;
        }

        if (action == "gate") {
            if (std::getenv("ICMG_NO_RITUAL_GATE")) return 0;
            if (!v.owed) return 0;
            std::string miss;
            for (RitualStep s : v.missing) { if (!miss.empty()) miss += ", "; miss += ritualStepName(s); }
            // Telemetry: record the block so `icmg discipline report` can show
            // gate-firing counts (measure the discipline, not just enforce it).
            core::RuleTelemetry::record("ritual-block", "", miss);
            // Stop-hook contract: decision:block forces the model to continue
            // and address the reason before the turn can end.
            std::cout <<
                "{\"decision\":\"block\",\"reason\":\"[icmg ritual] you edited code this "
                "session but the post-change sync is incomplete. Run the missing step(s): " << miss <<
                ". (icmg store --topic decisions-<area> \\\"what+why\\\"; icmg wflog save --goal \\\"...\\\" "
                "--decisions \\\"...\\\"). Graph auto-scans at turn end. Set ICMG_NO_RITUAL_GATE=1 to disable.\"}";
            return 0;
        }

        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("ritual", RitualCommand);

}  // namespace icmg::cli
