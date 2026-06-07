// 2026-06-07: pure helpers for `icmg msg check` (luna idea "wire --check": peek unread
// without manual --since). No IO. Marker stores the last-seen ts per session identity.
#pragma once
#include <string>
#include <cctype>

namespace icmg::core {

// Per-identity seen-marker filename: ".seen-<sanitized-me>". Non-alnum -> '_' so an
// identity like "pid-1234" or an email-keyed id is always a safe filename.
inline std::string seenMarkerName(const std::string& me) {
    std::string s = ".seen-";
    for (char c : me) s += (std::isalnum((unsigned char)c) ? c : '_');
    return s;
}

// Parse a stored cursor ts; empty/garbage -> 0 (show everything on first check).
inline long long parseSeenTs(const std::string& raw) {
    try { return std::stoll(raw); } catch (...) { return 0; }
}

} // namespace icmg::core
