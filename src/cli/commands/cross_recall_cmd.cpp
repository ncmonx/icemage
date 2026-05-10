// Phase 72 T9: `icmg cross-recall <prompt>` — explicit cross-project memory
// lookup. Wraps `icmg recall --all-projects` with project-attribution +
// "completed task" framing. Surfaces "this was done in project X on date Y".

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <iostream>
#include <string>

namespace icmg::cli {

class CrossRecallCommand : public BaseCommand {
public:
    std::string name()        const override { return "cross-recall"; }
    std::string description() const override {
        return "Cross-project memory recall — find tasks solved in other projects";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg cross-recall <prompt> [options]\n\n"
            "Searches all registered projects for memory hits matching prompt.\n"
            "Each result tagged [project-name] so you can see which project solved\n"
            "a similar task before. Useful for: 'this was done in project X — reuse'.\n\n"
            "Options:\n"
            "  --limit N         Max results (default 5)\n"
            "  --json            Machine-readable output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        // Build prompt from non-flag args.
        std::string prompt;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            if (!prompt.empty()) prompt += " ";
            prompt += a;
        }
        if (prompt.empty()) { std::cerr << "icmg cross-recall: requires <prompt>\n"; return 1; }

        int limit = 5;
        try { limit = std::stoi(flagValue(args, "--limit", "5")); } catch (...) {}
        bool json_out = hasFlag(args, "--json");

        // Delegate to existing `icmg recall --all-projects` for single source of truth.
        std::string esc;
        for (char c : prompt) {
            if (c == '"' || c == '\\') esc += '\\';
            esc += c;
        }
        std::string cmd = "icmg recall \"" + esc + "\" --all-projects --limit "
                        + std::to_string(limit);
        if (json_out) cmd += " --json";
        auto res = core::safeExecShell(cmd, false, 15000);
        if (res.exit_code != 0) {
            std::cerr << "icmg cross-recall: " << res.err;
            return res.exit_code;
        }
        if (!json_out) {
            std::cout << "icmg cross-recall: \"" << prompt << "\"\n";
            std::cout << std::string(64, '=') << "\n";
        }
        std::cout << res.out;
        if (!json_out && res.out.find("[") == std::string::npos) {
            std::cout << "\nNo cross-project hits. (Each project must be registered:\n"
                      << "  icmg project register <name> <path>)\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("cross-recall", CrossRecallCommand);

} // namespace icmg::cli
