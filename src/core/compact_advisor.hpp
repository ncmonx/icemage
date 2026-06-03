#pragma once
// v2.0.0 C5: idle-compact advisor. When context fill >= threshold AND has crossed into a
// new 10% band since the last nudge, emit an advisory to run /compact now (a natural idle
// moment, not the forced mid-task wall). Hooks cannot trigger /compact (anthropics/
// claude-code#58538), so this is ADVISORY ONLY. Band rate-limit avoids per-turn nagging.
// Pure.
#include <string>

namespace icmg::core {

struct Nudge {
    bool fire = false;
    int  band = -1;
    std::string message;
};

inline Nudge idleCompactAdvice(int fillPct, int lastFiredBand, int thresholdPct) {
    Nudge n;
    if (fillPct < thresholdPct) return n;        // not high enough
    int band = fillPct / 10;
    if (band <= lastFiredBand) return n;          // already nudged in this (or lower) band
    n.fire = true;
    n.band = band;
    n.message = "[icmg] context " + std::to_string(fillPct) +
                "% - good idle moment to /compact now (lossless: pinned rules re-anchored).";
    return n;
}

}  // namespace icmg::core
