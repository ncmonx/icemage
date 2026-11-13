// 2026-11-12: byte-budget prefix that never splits a UTF-8 sequence. A plain
// s.substr(0, n) on Indonesian text with emoji / arrows leaves a dangling lead
// byte that renders as mojibake (and is invalid UTF-8 in JSON output).
#pragma once
#include <string>

namespace icmg::core {

// Longest prefix of `s` that is <= maxBytes and ends on a character boundary.
inline std::string utf8Prefix(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t end = maxBytes;
    // Step back over continuation bytes (10xxxxxx) to the lead byte, then drop
    // the lead byte too: its character does not fit.
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return s.substr(0, end);
}

}  // namespace icmg::core
