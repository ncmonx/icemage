// `icmg recall-gate` — pre-task recall gate (PRE-change enforcement).
//
// Third leg of the discipline trilogy (ritual=post-change, discipline=
// visibility, recall-gate=pre-task). Forces a recall/pack BEFORE the first
// edit of a COMPLEX task, killing the "edit a subsystem with zero context"
// anti-pattern. Pure verdict in ../recall_gate.hpp (unit-tested); complexity
// from classifyIntent (../think_directive.hpp, unit-tested); this adds the
// per-turn markers + the PreToolUse deny JSON.
//
//   icmg recall-gate arm --task "<prompt>"   New turn: classify the prompt
//                                            (complex marker) + clear recalled.
//                                            (UserPromptSubmit hook.)
//   icmg recall-gate mark                    Record that recall/pack/context ran
//                                            this turn. (PostToolUse:Bash hook.)
//   icmg recall-gate check                   If complex task + not recalled,
//                                            print PreToolUse deny JSON; else
//                                            nothing. (PreToolUse:Edit|Write.)
//   icmg recall-gate status                  Human-readable marker state.
//   icmg recall-gate clear                   Remove both markers.
//
// Opt out entirely with ICMG_NO_RECALL_GATE=1.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/rule_telemetry.hpp"
#include "../recall_gate.hpp"
#include "../think_directive.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

fs::path complexMarker()  { return fs::path(".icmg") / "recallgate-complex";  }
fs::path recalledMarker() { return fs::path(".icmg") / "recallgate-recalled"; }

void touch(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p); f << "1\n";
}

}  // namespace

class RecallGateCommand : public BaseCommand {
public:
    std::string name()        const override { return "recall-gate"; }
    std::string description() const override {
        return "Pre-task recall gate — require recall/pack before editing on a complex task";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg recall-gate <action>\n\n"
            "  arm --task \"<prompt>\"   New turn: set complex marker from the prompt,\n"
            "                          clear recalled (UserPromptSubmit hook)\n"
            "  mark                    Record recall/pack/context ran this turn (Bash hook)\n"
            "  check                   If complex && not recalled, print PreToolUse deny JSON\n"
            "  status                  Show marker state\n"
            "  clear                   Remove both markers\n\n"
            "Blocks the first Edit/Write of a COMPLEX task until a recall/pack runs.\n"
            "Simple tasks never block. Opt out: ICMG_NO_RECALL_GATE=1.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string action = args[0];

        if (action == "arm") {
            // New turn: reset recalled, (re)set complex from the prompt.
            std::string task = flagValue(args, "--task", "");
            std::error_code ec;
            fs::remove(recalledMarker(), ec);
            if (classifyIntent(task) == Intent::Complex) touch(complexMarker());
            else fs::remove(complexMarker(), ec);
            return 0;
        }

        if (action == "mark") {
            touch(recalledMarker());
            return 0;
        }

        if (action == "clear") {
            std::error_code ec;
            fs::remove(complexMarker(), ec);
            fs::remove(recalledMarker(), ec);
            std::cout << "icmg recall-gate: markers cleared\n";
            return 0;
        }

        bool complex  = fs::exists(complexMarker());
        bool recalled = fs::exists(recalledMarker());
        RecallGateVerdict v = recallGateVerdict(complex, recalled);

        if (action == "status") {
            std::cout << "icmg recall-gate: task=" << (complex ? "COMPLEX" : "simple")
                      << " recalled=" << (recalled ? "yes" : "no")
                      << " -> " << (v.block ? "WOULD BLOCK edits" : "allow") << "\n";
            return 0;
        }

        if (action == "check") {
            if (std::getenv("ICMG_NO_RECALL_GATE")) return 0;
            if (!v.block) return 0;
            // Telemetry: record the deny so `icmg discipline report` shows
            // gate-firing counts over time.
            core::RuleTelemetry::record("recall-gate-deny", "", "complex task, no recall this turn");
            // PreToolUse deny contract: deny + reason. The model recovers by
            // running `icmg pack "<task>"` (or recall) then retrying the edit.
            std::cout <<
                "{\"hookSpecificOutput\":{\"hookEventName\":\"PreToolUse\","
                "\"permissionDecision\":\"deny\",\"permissionDecisionReason\":"
                "\"[icmg recall-gate] complex task — run `icmg pack \\\"<task>\\\"` "
                "(or `icmg recall \\\"<q>\\\"`) BEFORE editing, so you build with "
                "context instead of blind. Set ICMG_NO_RECALL_GATE=1 to disable.\"}}";
            return 0;
        }

        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("recall-gate", RecallGateCommand);

}  // namespace icmg::cli
