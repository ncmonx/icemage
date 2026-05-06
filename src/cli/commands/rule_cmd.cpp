#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../rules/rule_store.hpp"
#include "../../rules/rule_resolver.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace icmg::cli {

// ----------------------------------------------------------------
// icmg rule add <scope_path> <type> <name> <content> [--update]
// icmg rule list [<path>] [--json] [--all]
// icmg rule apply <file>  [--json]
// icmg rule show <id>
// icmg rule remove <id>
// icmg rule enable <id>
// icmg rule disable <id>
// ----------------------------------------------------------------
class RuleCommand : public BaseCommand {
public:
    std::string name()        const override { return "rule"; }
    std::string description() const override { return "Per-folder rules with inheritance"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg rule <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  add <scope> <type> <name> <content> [--update]\n"
            "      Add rule. scope examples: \"/\" \"src/\" \"src/api/\"\n"
            "      types: coding|arch|workflow|model|custom\n"
            "  list [<path>] [--all] [--json]\n"
            "      List rules applicable to path (or all rules)\n"
            "  apply <file> [--json]\n"
            "      Show full inheritance chain for file\n"
            "  show <id>      Show rule by id\n"
            "  remove <id>    Hard-delete rule\n"
            "  enable <id>    Activate rule\n"
            "  disable <id>   Deactivate rule\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        rules::RuleStore store(db);
        rules::RuleResolver resolver(store);

        const std::string& sub = args[0];

        if (sub == "add")     return doAdd(store, args);
        if (sub == "list")    return doList(store, resolver, args);
        if (sub == "apply")   return doApply(resolver, args);
        if (sub == "show")    return doShow(store, args);
        if (sub == "remove")  return doRemove(store, args);
        if (sub == "enable")  return doSetActive(store, args, true);
        if (sub == "disable") return doSetActive(store, args, false);

        std::cerr << "icmg rule: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    // ---- add ---------------------------------------------------------------
    int doAdd(rules::RuleStore& store, const std::vector<std::string>& args) {
        // rule add <scope> <type> <name> <content> [--update]
        bool update = hasFlag(args, "--update");

        // Collect positional args (non-flag)
        std::vector<std::string> pos;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--update") continue;
            pos.push_back(args[i]);
        }

        if (pos.size() < 4) {
            std::cerr << "icmg rule add: requires <scope> <type> <name> <content>\n";
            return 1;
        }

        rules::Rule rule;
        rule.scope_path = pos[0];
        rule.rule_type  = pos[1];
        rule.name       = pos[2];
        // Content: join remaining tokens
        for (size_t i = 3; i < pos.size(); ++i) {
            if (!rule.content.empty()) rule.content += " ";
            rule.content += pos[i];
        }

        try {
            int64_t id = store.add(rule, update);
            std::cout << "Rule added: id=" << id << "\n";
        } catch (const rules::RuleConflictError& e) {
            std::cerr << "Conflict: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    // ---- list --------------------------------------------------------------
    int doList(rules::RuleStore& store, rules::RuleResolver& resolver,
               const std::vector<std::string>& args) {
        bool json_out = hasFlag(args, "--json");
        bool all      = hasFlag(args, "--all");

        // Optional path argument
        std::string path;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--json" || args[i] == "--all") continue;
            path = args[i];
        }

        std::vector<rules::Rule> list;
        if (all || path.empty()) {
            list = store.all();
        } else {
            list = resolver.resolve(path);
        }

        if (json_out) {
            std::cout << "[\n";
            for (size_t i = 0; i < list.size(); ++i) {
                auto& r = list[i];
                std::cout << "  {"
                    << "\"id\":" << r.id
                    << ",\"scope_path\":\"" << escJ(r.scope_path) << "\""
                    << ",\"rule_type\":\"" << escJ(r.rule_type) << "\""
                    << ",\"name\":\"" << escJ(r.name) << "\""
                    << ",\"content\":\"" << escJ(r.content) << "\""
                    << ",\"priority\":" << r.priority
                    << ",\"active\":" << (r.active ? "true" : "false")
                    << "}";
                if (i + 1 < list.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            if (list.empty()) {
                std::cout << "(no rules)\n"; return 0;
            }
            std::cout << std::left
                      << std::setw(4)  << "ID"
                      << std::setw(14) << "SCOPE"
                      << std::setw(10) << "TYPE"
                      << std::setw(20) << "NAME"
                      << "CONTENT\n"
                      << std::string(80, '-') << "\n";
            for (auto& r : list) {
                std::string cont = r.content.size() > 35
                    ? r.content.substr(0, 32) + "..."
                    : r.content;
                std::cout << std::setw(4)  << r.id
                          << std::setw(14) << r.scope_path
                          << std::setw(10) << r.rule_type
                          << std::setw(20) << r.name
                          << cont;
                if (!r.active) std::cout << "  [disabled]";
                std::cout << "\n";
            }
        }
        return 0;
    }

    // ---- apply -------------------------------------------------------------
    int doApply(rules::RuleResolver& resolver, const std::vector<std::string>& args) {
        bool json_out = hasFlag(args, "--json");
        std::string file;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--json") continue;
            file = args[i]; break;
        }
        if (file.empty()) {
            std::cerr << "icmg rule apply: requires <file>\n"; return 1;
        }

        auto chain  = resolver.resolve(file);
        auto confs  = resolver.conflicts(file);

        if (json_out) {
            // Emit chain as JSON, conflicts as separate array
            std::cout << "{\"file\":\"" << escJ(file) << "\",\"rules\":[\n";
            for (size_t i = 0; i < chain.size(); ++i) {
                auto& r = chain[i];
                std::cout << "  {\"id\":" << r.id
                    << ",\"scope\":\"" << escJ(r.scope_path) << "\""
                    << ",\"type\":\"" << escJ(r.rule_type) << "\""
                    << ",\"name\":\"" << escJ(r.name) << "\""
                    << ",\"content\":\"" << escJ(r.content) << "\"}";
                if (i + 1 < chain.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "],\"conflict_count\":" << confs.size() << "}\n";
            return 0;
        }

        std::cout << "Rules applicable to: " << file << "\n"
                  << "Inheritance chain (root → specific):\n\n";

        std::string cur_scope;
        for (auto& r : chain) {
            if (r.scope_path != cur_scope) {
                cur_scope = r.scope_path;
                std::cout << "[" << (cur_scope == "/" ? "ROOT /" : cur_scope) << "]\n";
            }
            std::cout << "  #" << r.id << " " << r.rule_type << ": " << r.name << "\n"
                      << "     \"" << r.content << "\"\n";
        }

        if (chain.empty()) std::cout << "  (no rules apply)\n";

        std::cout << "\nTotal: " << chain.size() << " rules active\n";

        // Show conflicts
        if (!confs.empty()) {
            std::cout << "\n";
            for (auto& [winner, loser] : confs) {
                std::cout << "⚠  Conflict at scope " << winner.scope_path << ":\n"
                          << "  #" << winner.id << " " << winner.rule_type
                          << ": " << winner.name << " (priority " << winner.priority
                          << ") — winner\n"
                          << "  #" << loser.id << " " << loser.rule_type
                          << ": " << loser.name << " (priority " << loser.priority
                          << ") ← conflict!\n"
                          << "  Resolution: higher priority wins; use --priority to override.\n";
            }
        }
        return 0;
    }

    // ---- show --------------------------------------------------------------
    int doShow(rules::RuleStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg rule show: requires <id>\n"; return 1; }
        int64_t id = 0;
        try { id = std::stoll(args[1]); } catch (...) {
            std::cerr << "invalid id\n"; return 1;
        }
        auto r = store.get(id);
        if (!r) { std::cerr << "rule not found: " << id << "\n"; return 1; }
        std::cout << "id:         " << r->id         << "\n"
                  << "scope_path: " << r->scope_path  << "\n"
                  << "rule_type:  " << r->rule_type   << "\n"
                  << "name:       " << r->name         << "\n"
                  << "priority:   " << r->priority     << "\n"
                  << "active:     " << (r->active ? "yes" : "no") << "\n"
                  << "content:\n  " << r->content      << "\n";
        return 0;
    }

    // ---- remove ------------------------------------------------------------
    int doRemove(rules::RuleStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg rule remove: requires <id>\n"; return 1; }
        int64_t id = 0;
        try { id = std::stoll(args[1]); } catch (...) {
            std::cerr << "invalid id\n"; return 1;
        }
        store.remove(id);
        std::cout << "Removed rule " << id << "\n";
        return 0;
    }

    // ---- enable/disable ----------------------------------------------------
    int doSetActive(rules::RuleStore& store, const std::vector<std::string>& args, bool active) {
        if (args.size() < 2) {
            std::cerr << "icmg rule " << (active ? "enable" : "disable")
                      << ": requires <id>\n";
            return 1;
        }
        int64_t id = 0;
        try { id = std::stoll(args[1]); } catch (...) {
            std::cerr << "invalid id\n"; return 1;
        }
        store.setActive(id, active);
        std::cout << "Rule " << id << (active ? " enabled" : " disabled") << "\n";
        return 0;
    }

    // ---- helpers -----------------------------------------------------------
    static std::string escJ(const std::string& s) {
        std::string out;
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else                out += c;
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("rule", RuleCommand);

} // namespace icmg::cli
