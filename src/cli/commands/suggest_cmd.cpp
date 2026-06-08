// `icmg suggest "<intent>"` — recommend the most relevant icmg command(s) for a
// natural-language intent. Surfaces the long tail of rarely-remembered commands so
// they actually get used. Ranks the LIVE command registry (name + description) via
// the pure model-free promptJaccard scorer.
//   icmg suggest "trace what depends on this function"  [--top N] [--json]
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/command_suggest.hpp"
#include "../registry_docs.hpp"
#include "../../core/stdin_util.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class SuggestCommand : public BaseCommand {
public:
    std::string name() const override { return "suggest"; }
    std::string description() const override {
        return "Recommend the icmg command(s) that best match a natural-language intent";
    }
    void usage() const override {
        std::cout << "Usage: icmg suggest \"<intent>\" [--top N] [--json]\n"
                     "       icmg suggest --hook [--gate F]   (UserPromptSubmit: 1-line gated hint from stdin)\n";
    }

    int run(const std::vector<std::string>& args) override {
        std::string intent;
        int top = 5;
        bool js = false, hook = false;
        double gate = 0.5;  // hook mode: fire only on a strong (name-level) match
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--top" && i + 1 < args.size()) {
                try { top = std::stoi(args[++i]); } catch (...) {}
            } else if (args[i] == "--gate" && i + 1 < args.size()) {
                try { gate = std::stod(args[++i]); } catch (...) {}
            } else if (args[i] == "--json") {
                js = true;
            } else if (args[i] == "--hook") {
                hook = true;
            } else if (!args[i].empty() && args[i][0] != '-' && intent.empty()) {
                intent = args[i];
            }
        }

        // Hook mode: read the user prompt from stdin (UserPromptSubmit payload),
        // surface at most ONE high-confidence command hint, else stay silent.
        if (hook) return runHook(gate);

        if (intent.empty()) { usage(); return 1; }
        if (top < 1) top = 1;

        // Rank the live command registry (skip self to avoid noise).
        auto& reg = core::Registry<BaseCommand>::instance();
        auto hits = core::rankCommands(intent, registryDocs("suggest"), top);

        if (js) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& h : hits) arr.push_back({{"command", h.name}, {"score", h.score}});
            std::cout << arr.dump(2) << "\n";
            return 0;
        }
        if (hits.empty()) {
            std::cout << "(no matching command -- try different words, or `icmg --help`)\n";
            return 0;
        }
        std::cout << "Suggested for: " << intent << "\n";
        for (const auto& h : hits) {
            auto cmd = reg.create(h.name);
            std::cout << "  icmg " << h.name << "  -- "
                      << (cmd ? cmd->description() : std::string()) << "\n";
        }
        return 0;
    }

private:
    // Registry snapshot lives in cli::registryDocs (registry_docs.hpp) -- shared
    // with `icmg map` so the {name,desc} builder has one source of truth.

    // UserPromptSubmit hook: read the prompt from stdin (JSON {"prompt":...} or raw
    // text), and emit at most one high-confidence command hint. Opt out with
    // ICMG_NO_SUGGEST=1. Always returns 0 (a hook must never block the prompt).
    int runHook(double gate) const {
        if (std::getenv("ICMG_NO_SUGGEST")) return 0;
        std::string raw = core::slurpStdinSafe();
        if (raw.empty()) return 0;
        std::string prompt = raw;
        try {
            auto j = nlohmann::json::parse(raw);
            if (j.contains("prompt") && j["prompt"].is_string()) prompt = j["prompt"].get<std::string>();
        } catch (...) { /* not JSON -> treat as raw prompt text */ }

        auto hits = core::rankCommands(prompt, registryDocs("suggest"), 1);
        if (hits.empty() || hits[0].score < gate) return 0;
        auto cmd = core::Registry<BaseCommand>::instance().create(hits[0].name);
        std::cout << "[icmg] relevant command: icmg " << hits[0].name;
        if (cmd && !cmd->description().empty()) std::cout << "  (" << cmd->description() << ")";
        std::cout << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("suggest", SuggestCommand);

}  // namespace icmg::cli
