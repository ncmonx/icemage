#pragma once
// Identity-agnostic persona scaffold: neutral template slots seeded into the persona DB
// so any icmg (any user) is born with cross-session continuity zones. NO hardcoded names.
#include <string>
#include <vector>

namespace icmg::core {
class ProfileStore;

struct PersonaSlot {
    std::string zone, key, kind, placeholder;
};

// The canonical neutral slot set (>=7 zones). Identity-agnostic by contract (tested).
const std::vector<PersonaSlot>& personaSlots();

// Seed slots for `user`. If force=false, only writes slots that are MISSING (existing slots
// preserved -> idempotent, user content safe). force=true overwrites all slots to template.
// Returns number of slots written.
int scaffoldPersona(ProfileStore& ps, const std::string& user, bool force);
}
