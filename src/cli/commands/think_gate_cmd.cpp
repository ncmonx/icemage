// `icmg think-gate "<prompt>"` — expose classifyIntent (think_directive) as a
// CLI so the UserPromptSubmit hook drives its skip-thinking decision from the
// ONE tested classifier instead of a duplicate bash regex.
//
//   icmg think-gate "<prompt>"          -> prints: simple | complex | unknown
//   icmg think-gate --hint "<prompt>"   -> if simple, prints the SessionStart
//                                          additionalContext JSON (skip-thinking
//                                          hint); else prints nothing.
//
// Single source of truth: SIMPLE_KEYWORDS / COMPLEX_KEYWORDS live in
// think_directive.cpp (unit-tested); the hook no longer re-implements them.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../think_directive.hpp"
#include <iostream>
#include <string>

namespace icmg::cli {

class ThinkGateCommand : public BaseCommand {
public:
    std::string name()        const override { return "think-gate"; }
    std::string description() const override {
        return "Classify a prompt (simple/complex) to drive skip-thinking — single source of truth";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg think-gate [--hint] \"<prompt>\"\n\n"
            "  (no flag)   Print classification: simple | complex | unknown\n"
            "  --hint      If simple, print the UserPromptSubmit additionalContext\n"
            "              JSON that hints Claude to skip the thinking block; else\n"
            "              print nothing. Used by the icmg-first hook.\n\n"
            "Classifier (SIMPLE/COMPLEX keywords + word-count) lives in\n"
            "think_directive.cpp and is unit-tested. COMPLEX wins on tie.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        bool hint = hasFlag(args, "--hint");

        // Join non-flag args as the prompt.
        std::string prompt;
        for (const auto& a : args) {
            if (a == "--hint") continue;
            if (!prompt.empty()) prompt += " ";
            prompt += a;
        }

        Intent intent = classifyIntent(prompt);

        if (!hint) {
            std::cout << intentLabel(intent) << "\n";
            return 0;
        }

        // --hint: only fire for clearly-simple prompts (skip-thinking is safe).
        if (intent == Intent::Simple) {
            // Symmetric: simple prompt -> skip thinking AND keep response terse.
            std::cout <<
                "{\"hookSpecificOutput\":{\"hookEventName\":\"UserPromptSubmit\","
                "\"additionalContext\":\"[icmg] simple/mechanical prompt -> skip the "
                "thinking block AND keep the response terse this turn "
                "(set ICMG_NO_AUTO_THINK=1 to disable)\"}}";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("think-gate", ThinkGateCommand);

}  // namespace icmg::cli
