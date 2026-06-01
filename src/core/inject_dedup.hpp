// v1.14.0: hook injection deduplication.
//
// Tracks SHA1 of injected `additionalContext` strings per logical session.
// Second emit of same content within session window returns empty string,
// saving 20-40% inject tokens. Session boundary = service start OR explicit
// reset via `Dedup::resetSession()`.

#pragma once

#include <string>

namespace icmg::core::inject_dedup {

// Returns true if this hash was seen before within current session window.
// First call: records hash, returns false (caller proceeds with full inject).
// Repeat: returns true (caller should emit empty).
bool seenBefore(const std::string& content);

// Reset dedup state — e.g. on SessionStart hook fire.
void resetSession();

// Number of unique hashes seen since last reset.
size_t uniqueCount();

}  // namespace icmg::core::inject_dedup
