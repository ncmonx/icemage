// `icmg map <cmd>` -- a command's "you-are-here" hallway map: show the related
// commands derived from the live registry (name+desc similarity via neighborsOf).
// Surfaces capabilities at decision-time so features get found + reused, not
// rebuilt. Unknown <cmd> -> nearest commands by treating the word as an intent.
//   icmg map context-budget          you-are-here + related (savings/govern/...)
//   icmg map "trace impact" --top 8  free intent -> nearest commands
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/command_suggest.hpp"
#include "../registry_docs.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class MapCommand : public BaseCommand {
public:
    std::string name() const override { return "map"; }
    std::string description() const override {
        return "Show a command's related neighbors (you-are-here map) to find + reuse features";
    }
    void usage() const override {
        std::cout << "Usage: icmg map <cmd> [--top N]\n"
                     "  Lists commands related to <cmd> (derived from name+desc).\n"
                     "  Unknown <cmd> -> nearest commands by intent.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") { usage(); return args.empty() ? 1 : 0; }
        std::string cmd = args[0];
        int top = 6;
        for (size_t i = 1; i + 1 < args.size(); ++i)
            if (args[i] == "--top") { try { top = std::stoi(args[i + 1]); } catch (...) {} }
        if (top < 1) top = 1;

        auto docs = registryDocs();   // full registry; neighborsOf drops the queried cmd
        bool known = false; std::string desc;
        for (const auto& d : docs) if (d.name == cmd) { known = true; desc = d.desc; break; }
        if (known) std::cout << "you are here: icmg " << cmd << "  --  " << desc << "\n";
        else       std::cout << "no exact command '" << cmd << "'; nearest by intent:\n";

        auto nb = core::neighborsOf(cmd, docs, top);
        if (nb.empty()) { std::cout << "  (no related commands found)\n"; return 0; }
        std::cout << "related:\n";
        for (const auto& h : nb) {
            std::cout << "  icmg " << h.name;
            for (const auto& d : docs) if (d.name == h.name) { std::cout << "  --  " << d.desc; break; }
            std::cout << "\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("map", MapCommand);

}  // namespace icmg::cli
