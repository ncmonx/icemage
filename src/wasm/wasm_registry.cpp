#include "wasm_registry.hpp"
#include "../core/persona_db.hpp"
#include "../core/profile_store.hpp"
#include "../core/user_identity.hpp"

namespace icmg::wasm {

std::optional<WasmSkill> matchWasmSkill(const std::string& command) {
    try {
        if (!icmg::core::personaDbAvailable()) return std::nullopt;
        std::string user = icmg::core::currentUser();
        icmg::core::ProfileStore ps(icmg::core::personaDb());
        for (auto& row : ps.listZone(user, WASM_ZONE)) {
            std::string err;
            auto s = parseSkillManifest(row.content, err);
            if (s && matchesCommand(s->match, command)) return s;
        }
    } catch (...) { /* fail-open */ }
    return std::nullopt;
}

} // namespace icmg::wasm
