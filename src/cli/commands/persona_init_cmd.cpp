// `icmg persona init [--force]` -- seed identity-agnostic continuity zones into persona DB.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/global_db.hpp"
#include "../../core/profile_store.hpp"
#include "../../core/persona_template.hpp"
#include "../../core/user_identity.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class PersonaCommand : public BaseCommand {
public:
    std::string name() const override { return "persona"; }
    std::string description() const override { return "Persona continuity zones (init scaffold)"; }
    void usage() const override {
        std::cout << "Usage: icmg persona init [--force]\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] != "init") { usage(); return 1; }
        bool force = false;
        for (size_t i = 1; i < args.size(); ++i) if (args[i] == "--force") force = true;
        std::string user = core::currentUser();
        core::Db& db = core::personaDbAvailable() ? core::personaDb()
                                                  : core::GlobalDb::instance().db();
        core::ProfileStore ps(db);
        int n = core::scaffoldPersona(ps, user, force);
        std::cout << "[persona init] " << n << " zona di-seed"
                  << (force ? " (force)" : "") << ". Isi tiap zona lewat `icmg profile add`.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("persona", PersonaCommand);
}
