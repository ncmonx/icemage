// v1.42.0 persona consumer impl.
#include "persona_loader.hpp"
#include "global_db.hpp"
#include "user_identity.hpp"
#include "db.hpp"
#include <format>

namespace icmg::core {

std::string buildPersonaPrefixFor(const std::string& user_id) {
    try {
        auto& gdb = GlobalDb::instance();
        gdb.init();  // idempotent; ensures migration v6 applied
        std::string persona, traits;
        gdb.db().query("SELECT persona, traits FROM user_personas WHERE user_id=?",
                       {user_id},
                       [&](const Row& r) {
                           if (r.size() >= 2) {
                               persona = r[0];
                               traits  = r[1];
                           }
                       });
        if (persona.empty()) return {};
        if (traits.empty()) {
            return std::format("[Persona: {}]\n\n", persona);
        }
        return std::format("[Persona: {} | Traits: {}]\n\n", persona, traits);
    } catch (...) {
        return {};  // fail-open — never block chat/agent on persona errors
    }
}

std::string buildPersonaPrefix() {
    return buildPersonaPrefixFor(currentUser());
}

}  // namespace icmg::core
