// `icmg statusline` -- one-line status for Claude Code's statusLine integration.
// CC pipes a JSON blob (with transcript_path) on stdin and renders whatever the
// command prints in the status bar. We turn the transcript's real API usage into
// a per-model-honest "ctx N%" reading so the budget is visible every turn.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/stdin_util.hpp"
#include "../statusline.hpp"
#include "../context_budget.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

namespace icmg::cli {

class StatuslineCommand : public BaseCommand {
public:
    std::string name()        const override { return "statusline"; }
    std::string description() const override {
        return "Emit a compact status line for Claude Code's statusLine integration";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg statusline [--transcript PATH]\n\n"
            "Reads Claude Code's statusLine JSON on stdin (transcript_path) and\n"
            "prints one line:  icmg <model> | ctx N% | used/limit  -- per-model\n"
            "honest context window. Wire into ~/.claude/settings.json:\n"
            "  \"statusLine\": { \"type\": \"command\", \"command\": \"icmg statusline\" }\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        std::string tp = flagValue(args, "--transcript");
        if (tp.empty()) {
            // Parse CC's statusLine JSON from stdin (fail-soft on anything odd).
            std::string body = core::slurpStdinSafe();
            if (!body.empty()) {
                try {
                    auto j = nlohmann::json::parse(body, nullptr, false);
                    if (!j.is_discarded()) tp = j.value("transcript_path", std::string(""));
                } catch (...) {}
            }
        }
        if (tp.empty()) { std::cout << "icmg | ctx ?\n"; return 0; }  // fail-soft

        long long used  = lastContextTokensFromTranscript(tp);
        long long limit = resolveContextLimit(tp);
        std::string model = modelShortName(lastModelFromTranscript(tp));
        std::cout << formatStatusline(computeBudget(used, limit), model) << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("statusline", StatuslineCommand);

}  // namespace icmg::cli
