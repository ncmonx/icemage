// v1.42.0 persona consumer impl.
#include "persona_loader.hpp"
#include "global_db.hpp"
#include "persona_db.hpp"
#include "user_identity.hpp"
#include "db.hpp"
#include <format>

namespace icmg::core {

std::string buildPersonaPrefixFor(const std::string& user_id) {
    try {
        // v1.57 S2: persona now lives at <exe-dir>/icmg-persona.db (shared
        // across Win users). readPersona tries exe-dir first, then falls
        // back to the legacy global DB so pre-v1.57 personas still load.
        std::string persona, traits;
        readPersona(user_id, persona, traits);
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
