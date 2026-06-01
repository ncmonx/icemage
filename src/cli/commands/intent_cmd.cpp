// v1.37.0 C2: `icmg intent` admin cmd for intent cache.
//
//   classify <prompt>         — regex+cache lookup, prints intent
//   regex    <prompt>         — pure regex (no DB)
//   stats                     — cache size + queue depth
//   clear                     — DELETE all
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/intent_cache.hpp"
#include "../../core/result.hpp"  // v1.40.1: std::expected pilot

// v1.40.0 C++23: std::format pilot. v1.40.1: std::print attempt REVERTED
// (libstdc++ 15.2 MinGW lacks std::__open_terminal/__write_to_terminal
// — POSIX-only symbols; link fails on Win). v1.40.1 extends std::format
// adoption + std::expected pilot via tryClearAll(). Re-evaluate std::print
// after libstdc++/MinGW ships terminal helpers.
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

// v1.40.1 C++23 std::expected pilot. tryClearAll() wraps the legacy
// int-return IntentCache::clearAll() and exposes Result<void> semantics.
// Future v1.40.4 bulk conversion will lift this pattern into the core
// layer once Result<> is proven on hot path.
static core::Result<int> tryClearAll() {
    int rc = core::IntentCache::clearAll();
    if (rc != 0) return core::err(rc, "intent cache clearAll failed");
    return rc;
}

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
            // v1.40.0 C++23 std::format adoption.
            std::cout << std::format("{{\"intent\":\"{}\",\"hash\":\"{}\"}}\n",
                                     core::intentName(it),
                                     core::IntentCache::hashPrompt(p));
            return 0;
        }
        if (sub == "stats") {
            // v1.40.1 C++23 std::format expansion (std::print reverted —
            // libstdc++ 15.2 MinGW missing terminal helpers).
            std::cout << std::format(
                "icmg intent stats\n"
                "  cache_size:   {}\n"
                "  queue_depth:  {}\n",
                core::IntentCache::cacheSize(),
                core::IntentCache::queueDepth());
            return 0;
        }
        if (sub == "clear") {
            // v1.40.1 C++23 std::expected pilot via tryClearAll().
            auto r = tryClearAll();
            if (!r) {
                std::cout << std::format("{{\"ok\":false,\"error\":\"{}\"}}\n", r.error().msg);
                return r.error().code;
            }
            std::cout << "{\"ok\":true}\n";
            return 0;
        }
        std::cerr << "intent: unknown subcommand '" << sub << "'\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("intent", IntentCommand);

} // namespace icmg::cli
