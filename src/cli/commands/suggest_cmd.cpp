// `icmg suggest "<intent>"` — recommend the most relevant icmg command(s) for a
// natural-language intent. Surfaces the long tail of rarely-remembered commands so
// they actually get used. Ranks the LIVE command registry (name + description) via
// the pure model-free promptJaccard scorer.
//   icmg suggest "trace what depends on this function"  [--top N] [--json]
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/command_suggest.hpp"
#include <nlohmann/json.hpp>
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
        std::cout << "Usage: icmg suggest \"<intent>\" [--top N] [--json]\n";
    }

    int run(const std::vector<std::string>& args) override {
        std::string intent;
        int top = 5;
        bool js = false;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--top" && i + 1 < args.size()) {
                try { top = std::stoi(args[++i]); } catch (...) {}
            } else if (args[i] == "--json") {
                js = true;
            } else if (!args[i].empty() && args[i][0] != '-' && intent.empty()) {
                intent = args[i];
            }
        }
        if (intent.empty()) { usage(); return 1; }
        if (top < 1) top = 1;

        // Build docs from the live command registry (skip self to avoid noise).
        auto& reg = core::Registry<BaseCommand>::instance();
        std::vector<core::CmdDoc> docs;
        for (const auto& k : reg.keys()) {
            if (k == "suggest") continue;
            auto cmd = reg.create(k);
            docs.push_back({k, cmd ? cmd->description() : std::string()});
        }

        auto hits = core::rankCommands(intent, docs, top);

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
};

ICMG_REGISTER_COMMAND("suggest", SuggestCommand);

}  // namespace icmg::cli
