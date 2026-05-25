// v1.42.0 persona consumer helpers. Reads current user's persona from
// global DB and builds a system-prompt prefix for chat/agent LLM paths.
// Returns empty string when no persona set (passthrough behaviour).
#pragma once

#include <string>

namespace icmg::core {

// Build "Persona: <name>\nTraits: <traits>\n\n" prefix for current user,
// or empty string if no persona configured. Safe to call repeatedly —
// lazy-inits global DB on first call.
std::string buildPersonaPrefix();

// Same as above but for a specific user_id (admin/test paths).
std::string buildPersonaPrefixFor(const std::string& user_id);

}  // namespace icmg::core
