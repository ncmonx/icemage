// v1.53.0: icmg route classify <prompt> — emit RouteDecision for hooks.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../router.hpp"
#include <iostream>
#include <sstream>
#include <string>

namespace icmg::cli {

class RouteCommand : public BaseCommand {
public:
    std::string name()        const override { return "route"; }
    std::string description() const override { return "Smart prompt classifier (LOCAL/CLOUD/CACHE)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg route <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  classify <prompt>           Print routing decision (text).\n"
            "  classify --json <prompt>    Emit decision as JSON.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }
        if (args[0] != "classify") {
            std::cerr << "route: unknown subcommand: " << args[0] << "\n";
            return 2;
        }
        bool json = false;
        size_t idx = 1;
        if (idx < args.size() && args[idx] == "--json") { json = true; ++idx; }
        if (idx >= args.size()) {
            std::cerr << "Usage: icmg route classify [--json] <prompt>\n";
            return 2;
        }
        // Join remaining args into single prompt.
        std::string prompt;
        for (size_t i = idx; i < args.size(); ++i) {
            if (!prompt.empty()) prompt += " ";
            prompt += args[i];
        }
        auto d = classifyPrompt(prompt);
        const char* route_str = (d.route == Route::LOCAL ? "LOCAL"
                                : d.route == Route::CLOUD ? "CLOUD" : "CACHE");
        if (json) {
            std::cout << "{\"route\":\"" << route_str
                      << "\",\"confidence\":" << d.confidence
                      << ",\"reason\":\"" << d.reason << "\"}\n";
        } else {
            std::cout << "route=" << route_str
                      << " confidence=" << d.confidence
                      << " reason=" << d.reason << "\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("route", RouteCommand);

}  // namespace icmg::cli
