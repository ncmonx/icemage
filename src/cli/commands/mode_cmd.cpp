// `icmg mode` — a persistent, cross-project "current session mode / focus" banner.
// Set it once; a UserPromptSubmit hook then prepends it to every turn so the agent
// always knows what state the session is in (e.g. "long-session test: ship-local,
// hunt bugs") without the user repeating it. Stored in the EXE-DIR persona DB (zone
// "_mode") so it never pollutes the project graph/memory DB and is shared across projects.
//   icmg mode set "<text>"   set the current mode
//   icmg mode get            print the current mode (empty if none)
//   icmg mode clear          clear it
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/global_db.hpp"
#include "../../core/profile_store.hpp"
#include "../../core/user_identity.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class ModeCommand : public BaseCommand {
public:
    std::string name() const override { return "mode"; }
    std::string description() const override {
        return "Persistent cross-project session mode banner (set/get/clear), injected each turn";
    }
    void usage() const override {
        std::cout << "Usage: icmg mode set \"<text>\" | get | clear\n";
    }

    int run(const std::vector<std::string>& args) override {
        const std::string sub = args.empty() ? "get" : args[0];
        const std::string user = core::currentUser();
        core::Db& db = core::personaDbAvailable() ? core::personaDb()
                                                  : core::GlobalDb::instance().db();
        core::ProfileStore ps(db);
        static const char* ZONE = "_mode";
        static const char* KEY  = "current";

        if (sub == "set") {
            std::string text;
            for (size_t i = 1; i < args.size(); ++i) { if (i > 1) text += " "; text += args[i]; }
            ps.put(user, ZONE, KEY, "note", text);
            std::cout << "[mode] set: " << text << "\n";
            return 0;
        }

        std::string content, kind;
        bool has = ps.get(user, ZONE, KEY, content, kind);

        if (sub == "get") { if (has && !content.empty()) std::cout << content << "\n"; return 0; }
        if (sub == "clear") { ps.forget(user, ZONE, KEY); std::cout << "[mode] cleared.\n"; return 0; }
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("mode", ModeCommand);

}  // namespace icmg::cli
