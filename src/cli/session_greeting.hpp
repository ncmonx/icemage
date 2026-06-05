// Session greeting hint — decouples the wake-up trigger phrase from literal
// time-of-day. Clearing the conversation is NOT a new day: it may be a few
// minutes apart. We compute the gap since the last handoff and tell the
// assistant whether to greet as a CONTINUATION ("lanjut") or a FRESH session
// ("pagi/sore" by wall-clock). Pure + header-only so it is trivially testable.
#pragma once

#include <cstdint>
#include <string>

namespace icmg::cli {

struct GreetingHint {
    int64_t     gapSec = 0;       // now - lastHandoffTs, clamped to >= 0
    bool        haveLast = false; // false when no handoff timestamp is available
    std::string mode;             // "continue" | "fresh" | "unknown"
};

// Under this gap, the screen reset is treated as the SAME working session
// (just a cleared conversation) → greet "continue". At/over it, the gap most
// likely spans an overnight/new day → greet "fresh".
inline constexpr int64_t kFreshSessionGapSec = 8 * 3600; // 8h

inline GreetingHint computeGreetingHint(int64_t now, int64_t lastHandoffTs, bool haveLast) {
    GreetingHint h;
    h.haveLast = haveLast;
    if (!haveLast) { h.mode = "unknown"; return h; }
    int64_t gap = now - lastHandoffTs;
    if (gap < 0) gap = 0; // clock-skew guard: never report a negative gap
    h.gapSec = gap;
    h.mode = (gap < kFreshSessionGapSec) ? "continue" : "fresh";
    return h;
}

// Compact human label: "~9m", "~3h", "~1d2h".
inline std::string formatGap(int64_t sec) {
    if (sec < 60)    return "<1m";
    if (sec < 3600)  return "~" + std::to_string(sec / 60) + "m";
    if (sec < 86400) return "~" + std::to_string(sec / 3600) + "h";
    int64_t d = sec / 86400;
    int64_t h = (sec % 86400) / 3600;
    return "~" + std::to_string(d) + "d" + std::to_string(h) + "h";
}

} // namespace icmg::cli
