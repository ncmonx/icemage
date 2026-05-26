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
        // v1.47.0: role-binding prefix. Identity tag (without negation/boundary
        // clauses — those will be customized by user).
        if (traits.empty()) {
            return std::format(
                "[SYSTEM] Kamu adalah AI assistant bernama \"{0}\".\n\n",
                persona);
        }
        return std::format(
            "[SYSTEM] Kamu adalah AI assistant bernama \"{0}\" dengan traits: {1}.\n"
            "Aturan jawaban: jangan pernah pakai placeholder seperti [Nama Anda], [X], {{name}}, atau bracket-template lain. Kalau belum tahu identitas user, tanya langsung dengan kalimat alami.\n\n",
            persona, traits);
    } catch (...) {
        return {};  // fail-open — never block chat/agent on persona errors
    }
}

std::string buildPersonaPrefix() {
    return buildPersonaPrefixFor(currentUser());
}

}  // namespace icmg::core
