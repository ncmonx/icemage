// v1.37.0 C2: `icmg intent` admin cmd for intent cache.
//
//   classify <prompt>         — regex+cache lookup, prints intent
//   regex    <prompt>         — pure regex (no DB)
//   stats                     — cache size + queue depth
//   clear                     — DELETE all
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/intent_cache.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class IntentCommand : public BaseCommand {
public:
    std::string name()        const override { return "intent"; }
    std::string description() const override {
        return "Intent cache admin (classify/regex/stats/clear)";
    }
    void usage() const override {
        std::cout <<
            "Usage: icmg intent <subcommand> [args]\n\n"
            "Subcommands:\n"
            "  classify <prompt>   Regex + cache lookup (hot-path safe <2ms)\n"
            "  regex    <prompt>   Pure regex classify (no DB)\n"
            "  stats               Cache size + backfill queue depth\n"
            "  clear               DELETE all rows (admin reset)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        const std::string& sub = args[0];
        if (sub == "classify" || sub == "regex") {
            if (args.size() < 2) { std::cerr << "intent " << sub << ": <prompt> required\n"; return 1; }
            std::ostringstream o;
            for (std::size_t i = 1; i < args.size(); ++i) { if (i > 1) o << ' '; o << args[i]; }
            std::string p = o.str();
            auto it = (sub == "regex") ? core::IntentCache::classifyRegex(p)
                                       : core::IntentCache::classify(p);
            std::cout << "{\"intent\":\"" << core::intentName(it)
                      << "\",\"hash\":\""  << core::IntentCache::hashPrompt(p) << "\"}\n";
            return 0;
        }
        if (sub == "stats") {
            std::cout << "icmg intent stats\n";
            std::cout << "  cache_size:   " << core::IntentCache::cacheSize() << "\n";
            std::cout << "  queue_depth:  " << core::IntentCache::queueDepth() << "\n";
            return 0;
        }
        if (sub == "clear") {
            int rc = core::IntentCache::clearAll();
            std::cout << "{\"ok\":" << (rc == 0 ? "true" : "false") << "}\n";
            return rc;
        }
        std::cerr << "intent: unknown subcommand '" << sub << "'\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("intent", IntentCommand);

} // namespace icmg::cli
