#include "../core/hook_bus.hpp"
#include "../core/registry.hpp"
#include "../core/config.hpp"
#include "../core/db.hpp"
#include "rule_store.hpp"
#include "rule_resolver.hpp"
#include <filesystem>
#include <string>

namespace icmg::rules {

// PRE_STORE hook: auto-attach applicable rules context to stored memory.
// Reads CWD, queries rules for that scope, appends summary to content.
// Non-blocking — failure to load rules is silently ignored.
static void attachRulesContext(core::HookContext& ctx) {
    try {
        // Resolve CWD as scope path
        std::string cwd = std::filesystem::current_path().string();
        // Normalise: replace backslashes, ensure trailing slash
        for (char& c : cwd) if (c == '\\') c = '/';
        if (!cwd.empty() && cwd.back() != '/') cwd += '/';

        auto& cfg = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        core::Db db(db_path);

        RuleStore store(db);
        RuleResolver resolver(store);

        auto applicable = resolver.resolve(cwd);
        if (applicable.empty()) return;

        // Build a compact rules summary to append to content
        std::string rules_ctx = "\n\n[Context rules for " + cwd + "]\n";
        for (auto& r : applicable) {
            rules_ctx += "- [" + r.rule_type + "/" + r.name + "] " + r.content + "\n";
        }

        // Append to existing content
        std::string content = ctx.get<std::string>("content", "");
        ctx.set<std::string>("content", content + rules_ctx);

    } catch (...) {
        // Silent — hook must not break store operation
    }
}

// Priority 20 — runs after abbreviation detector (10) but before default (50)
ICMG_REGISTER_HOOK(core::HookEvent::PRE_STORE, attachRulesContext, 20);

} // namespace icmg::rules
