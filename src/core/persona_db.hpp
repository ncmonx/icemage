// v1.57 S2: persona DB at icmg.exe directory.
//
// Persona is shared across ALL Windows users of one icmg install, so it
// lives next to the binary (<exe-dir>/icmg-persona.db) rather than in
// per-user %APPDATA%. Falls back to the global DB when the exe directory
// is not writable (e.g. a Program Files install without admin).
//
// Schema: a single user_personas table mirroring global_db migration 6.

#pragma once

#include <string>

namespace icmg::core {

class Db;

// Returns true if a usable persona DB exists at the exe directory (open +
// table ensured + ACL relaxed). When false, callers should fall back to
// GlobalDb. Lazily initialised on first call.
bool personaDbAvailable();

// Access the exe-dir persona DB. Throws std::runtime_error if unavailable
// (caller should guard with personaDbAvailable() or catch).
Db& personaDb();

// Read a persona row (persona + traits) for user_id. Tries the exe-dir DB
// first, then the legacy global DB. Returns true if found; out-params set.
bool readPersona(const std::string& user_id,
                 std::string& persona_out,
                 std::string& traits_out);

// Write/update a persona row. Prefers the exe-dir DB; falls back to global
// DB when exe-dir is not writable. Returns true on success.
bool writePersona(const std::string& user_id,
                  const std::string& persona,
                  const std::string& traits);

}  // namespace icmg::core
