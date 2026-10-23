// v1.35.0 R4: `icmg rule-viol` admin cmd for rule_violations table.
//
// Subcommands:
//   record <rule_id> [ctx]    — append a violation row (used by leash bash hook later)
//   stats                     — top-N rules by violation count
//   list [--limit N]          — recent violations
//   clear                     — drop all rows
#include "../base_command.hpp"
#include "../action_taxonomy.hpp"   // v2.26.0 leash audit
#include "../../core/registry.hpp"
#include "../../core/rule_telemetry.hpp"

#include <ctime>
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class RuleViolCommand : public BaseCommand {
public:
    std::string name()        const override { return "rule-viol"; }
    std::string description() const override {
        return "Rule violation telemetry (record/stats/list/clear)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg rule-viol <subcommand> [args]\n\n"
            "Subcommands:\n"
            "  record <rule_id> [ctx] [--session S]\n"
            "                           Append violation row (called from leash hook).\n"
            "                           ctx is auto-classified to an action class\n"
            "                           (git.push.force, fs.rm.recursive, ...) and S\n"
            "                           is recorded as the ACTOR (agent session).\n"
            "  stats [--limit N]        Top-N rules by count (default 5)\n"
            "  list  [--limit N]        Recent violations, actor-attributed (default 20)\n"
            "  clear                    DELETE all rows (admin reset)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        const std::string& sub = args[0];
        if (sub == "record") {
            if (args.size() < 2) { std::cerr << "rule-viol record: <rule_id> required\n"; return 1; }
            // v2.26.0: actor attribution + action taxonomy (zabta.ai lesson:
            // audit must name the AGENT session, not the human owner).
            std::string sess = flagValue(args, "--session");
            std::string ctx  = (args.size() > 2 && args[2].rfind("--", 0) != 0) ? args[2] : "";
            std::string action = classifyAction(ctx);
            if (!action.empty()) ctx = "[action=" + action + "] " + ctx;
            core::RuleTelemetry::record(args[1], sess, ctx);
            std::cout << "{\"ok\":true,\"action\":\"" << action << "\"}\n";
            return 0;
        }
        if (sub == "list") {
            int limit = 20;
            try { limit = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
            auto rows = core::RuleTelemetry::recent(limit);
            std::cout << "icmg rule-viol list (" << rows.size() << " recent, newest first):\n";
            for (const auto& r : rows) {
                std::cout << "  [" << r.at << "] " << r.rule_id
                          << "  actor=" << r.session_id
                          << "  " << r.ctx.substr(0, 100) << "\n";
            }
            return 0;
        }
        if (sub == "stats") {
            int limit = 5;
            try { limit = std::stoi(flagValue(args, "--limit", "5")); } catch (...) {}
            auto top = core::RuleTelemetry::topByCount(limit);
            std::cout << "icmg rule-viol stats (top " << top.size() << " by count):\n";
            for (const auto& s : top) {
                std::cout << "  " << s.rule_id
                          << "  count=" << s.count_total
                          << "  last=" << s.last_at
                          << "  ctx=\"" << s.last_ctx.substr(0, 60) << "\"\n";
            }
            return 0;
        }
        if (sub == "clear") {
            int rc = core::RuleTelemetry::clearAll();
            std::cout << "{\"ok\":" << (rc == 0 ? "true" : "false") << "}\n";
            return rc;
        }
        std::cerr << "rule-viol: unknown subcommand '" << sub << "'\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("rule-viol", RuleViolCommand);

} // namespace icmg::cli
