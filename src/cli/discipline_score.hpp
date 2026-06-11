#pragma once
// Feature-coverage scorecard — VISIBILITY-side enforcement (2026-06-11).
//
// `ritual` (post-change gate) and `recall-gate` (pre-task gate) are the HARD
// teeth. This is the soft, motivational counterpart: a per-session tally of
// which discipline-critical icmg features the model actually touched, so the
// "you have 22 features but only ever use 3" blind spot becomes a visible
// scorecard (surfaced at SessionStart + on demand via `icmg discipline`).
//
// PURE CORE — no I/O — unit-testable. The command wraps it with a session
// usage ledger fed by the PostToolUse:Bash hook.

#include <set>
#include <string>
#include <vector>

namespace icmg::cli {

// The discipline-critical features a well-run session is expected to touch.
// Curated (not every command) — these are the ones whose absence signals the
// model is flying blind (no recall), skipping persistence (no store/wflog), or
// bypassing token filters (no run/context). Kept ~12 so the % is meaningful.
inline const std::vector<std::string>& disciplineCoreFeatures() {
    static const std::vector<std::string> core{
        "recall", "pack", "context", "graph", "store", "wflog",
        "verify", "zone", "run", "parallel", "fail", "memoir"};
    return core;
}

struct DisciplineScore {
    int used  = 0;                   // # core features touched this session
    int total = 0;                   // # core features
    int pct   = 0;                   // used*100/total (integer)
    std::vector<std::string> cold;   // core features NOT yet used (core order)
};

inline DisciplineScore scoreDiscipline(
        const std::set<std::string>& usedCmds,
        const std::vector<std::string>& core = disciplineCoreFeatures()) {
    DisciplineScore s;
    s.total = static_cast<int>(core.size());
    for (const auto& f : core) {
        if (usedCmds.count(f)) s.used++;
        else                   s.cold.push_back(f);
    }
    s.pct = s.total ? (s.used * 100 / s.total) : 0;
    return s;
}

// Qualitative band for a coverage % — the one-word verdict shown by
// `icmg discipline report`. Pure + testable (presentation logic stays out of
// the scorer). Bands: strong >=75, ok >=50, thin >=25, blind <25.
inline std::string disciplineGrade(int pct) {
    if (pct >= 75) return "strong";
    if (pct >= 50) return "ok";
    if (pct >= 25) return "thin";
    return "blind";
}

}  // namespace icmg::cli
