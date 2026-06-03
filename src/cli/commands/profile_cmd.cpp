// `icmg profile` — zoned profile/skill store in the exe-dir persona DB (cross-project).
//   profile add --zone Z --key K [--kind skill|note|profile] --content "..."
//   profile get --zone Z --key K
//   profile list --zone Z
//   profile search "<query>"
//   profile forget --zone Z --key K
// Content-neutral capability: stores whatever text the user supplies.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/global_db.hpp"
#include "../../core/profile_store.hpp"
#include "../../core/prompt_history.hpp"
#include "../../core/user_identity.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class ProfileCommand : public BaseCommand {
public:
    std::string name() const override { return "profile"; }
    std::string description() const override { return "Zoned profile/skill store (persona DB)"; }
    void usage() const override {
        std::cout << "Usage: icmg profile add|get|list|search|forget "
                     "[--zone Z --key K --kind skill|note|profile --content ...]\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { usage(); return 1; }
        const std::string sub = args[0];
        std::string user = core::currentUser();
        core::Db& db = core::personaDbAvailable() ? core::personaDb()
                                                  : core::GlobalDb::instance().db();
        core::ProfileStore ps(db);

        std::string zone, key, kind = "profile", content, query, prompt, response;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--zone" && i + 1 < args.size()) zone = args[++i];
            else if (args[i] == "--key" && i + 1 < args.size()) key = args[++i];
            else if (args[i] == "--kind" && i + 1 < args.size()) kind = args[++i];
            else if (args[i] == "--content" && i + 1 < args.size()) content = args[++i];
            else if (args[i] == "--prompt" && i + 1 < args.size()) prompt = args[++i];
            else if (args[i] == "--response" && i + 1 < args.size()) response = args[++i];
            else if ((sub == "search" || sub == "qa-find") && !args[i].empty() && args[i][0] != '-') query = args[i];
        }

        // ---- prompt->response history (T6) ----
        if (sub == "qa-add") {
            if (prompt.empty() || response.empty()) { std::cerr << "need --prompt and --response\n"; return 1; }
            core::PromptHistory ph(db);
            ph.record(user, zone, prompt, response);
            std::cout << "[profile qa-add] " << (zone.empty() ? "default" : zone)
                      << ": prompt recorded.\n";
            return 0;
        }
        if (sub == "qa-find") {
            core::PromptHistory ph(db);
            std::string exact;
            if (ph.recallExact(user, zone, query, exact)) {
                std::cout << "[exact] " << exact << "\n";
                return 0;
            }
            auto hits = ph.findSimilar(user, query, 5);
            if (hits.empty()) { std::cout << "(no similar prompt in history)\n"; return 0; }
            for (auto& h : hits)
                std::cout << "  ~ " << h.prompt.substr(0, 60) << " => "
                          << h.response.substr(0, 80) << "\n";
            return 0;
        }

        if (sub == "add") {
            if (key.empty() || content.empty()) { std::cerr << "need --key and --content\n"; return 1; }
            ps.put(user, zone, key, kind, content);
            std::cout << "[profile add] " << (zone.empty() ? "default" : zone) << "/" << key
                      << " (" << kind << ") saved.\n";
            return 0;
        }
        if (sub == "get") {
            std::string c, k;
            if (ps.get(user, zone, key, c, k)) { std::cout << "[" << k << "] " << c << "\n"; return 0; }
            std::cerr << "not found\n"; return 1;
        }
        if (sub == "list") {
            for (auto& r : ps.listZone(user, zone))
                std::cout << "  " << r.zone << "/" << r.key << " (" << r.kind << ")\n";
            return 0;
        }
        if (sub == "search") {
            for (auto& r : ps.search(user, query))
                std::cout << "  " << r.zone << "/" << r.key << ": " << r.content.substr(0, 80) << "\n";
            return 0;
        }
        if (sub == "forget") {
            ps.forget(user, zone, key);
            std::cout << "[profile forget] done.\n";
            return 0;
        }
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("profile", ProfileCommand);

}  // namespace icmg::cli
