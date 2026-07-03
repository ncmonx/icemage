// src/tkil/command_learn.hpp
// D6: cross-session learning. The `commands` table persists, across every
// session, per-command aggregates: frequency, total_original_lines,
// total_filtered_lines. This pure analyzer mines those accumulated stats to
// learn which commands are consistently "noisy" -- frequently run, large
// output, most of which the Tkil filter discards -- and recommends a tighter
// render mode (--nano for build/test diagnostics, --gist otherwise).
//
// Pure + header-only so it is unit-testable without a DB or linking icmg_lib.
// Read-only/advisory: it never changes behaviour on its own, it surfaces a
// recommendation the user (or a future auto-router) can act on.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::tkil {

// One row of accumulated per-command history (from the `commands` table).
struct CmdStat {
    std::string command;
    int64_t     frequency        = 0;  // times run across all sessions
    int64_t     total_original   = 0;  // sum of raw output lines
    int64_t     total_filtered   = 0;  // sum of lines surviving the filter
};

enum class LearnClass { Noisy, Normal, Quiet };

struct LearnResult {
    std::string command;
    LearnClass  cls           = LearnClass::Normal;
    double      avg_original  = 0.0;   // mean raw lines per run
    double      filter_ratio  = 1.0;   // filtered/original (1 = nothing removed)
    std::string recommendation;        // human-facing suggestion ("" if none)
};

// Tunable thresholds (defaults chosen conservative so we only flag commands
// with real, repeated evidence).
struct LearnConfig {
    int64_t min_frequency   = 3;    // need repeated observations to trust it
    double  big_avg_lines   = 40.0; // "large" output per run
    double  noisy_ratio_max = 0.6;  // <=60% survives filter -> mostly noise
    double  quiet_avg_lines = 8.0;  // below this, output is already tiny
};

// Heuristic: does this command look like a build/test/compile command? Those
// benefit from --nano (symbol-only diagnostics); everything else from --gist.
inline bool learnLooksLikeBuild(const std::string& command) {
    std::string c;
    c.reserve(command.size());
    for (char ch : command) c += (char)std::tolower((unsigned char)ch);
    for (const char* kw : {"build", "make", "cmake", "ninja", "cargo build",
                           "gcc", "clang", "msbuild", "test", "ctest",
                           "pytest", "jest", "vitest", "go test"}) {
        if (c.find(kw) != std::string::npos) return true;
    }
    return false;
}

// Classify + recommend for a single command's accumulated stats.
inline LearnResult analyzeOne(const CmdStat& s, const LearnConfig& cfg = {}) {
    LearnResult r;
    r.command = s.command;
    r.avg_original = s.frequency > 0
        ? (double)s.total_original / (double)s.frequency : 0.0;
    r.filter_ratio = s.total_original > 0
        ? (double)s.total_filtered / (double)s.total_original : 1.0;

    if (r.avg_original < cfg.quiet_avg_lines) {
        r.cls = LearnClass::Quiet;   // already tiny -- nothing to gain
        return r;
    }
    bool frequent = s.frequency >= cfg.min_frequency;
    bool large    = r.avg_original >= cfg.big_avg_lines;
    bool mostly_noise = r.filter_ratio <= cfg.noisy_ratio_max;
    if (frequent && large && mostly_noise) {
        r.cls = LearnClass::Noisy;
        r.recommendation = learnLooksLikeBuild(s.command)
            ? "run with --nano (symbol-only diagnostics)"
            : "run with --gist (one-line summary)";
    }
    return r;
}

// Analyze a whole corpus; return NOISY results only, sorted by "waste" (how
// many lines per run get discarded) descending -- the biggest wins first.
inline std::vector<LearnResult> analyzeCommands(const std::vector<CmdStat>& stats,
                                                const LearnConfig& cfg = {}) {
    std::vector<LearnResult> out;
    for (const auto& s : stats) {
        LearnResult r = analyzeOne(s, cfg);
        if (r.cls == LearnClass::Noisy) out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const LearnResult& a, const LearnResult& b) {
        double wa = a.avg_original * (1.0 - a.filter_ratio);
        double wb = b.avg_original * (1.0 - b.filter_ratio);
        return wa > wb;
    });
    return out;
}

inline const char* learnClassName(LearnClass c) {
    switch (c) {
        case LearnClass::Noisy: return "noisy";
        case LearnClass::Quiet: return "quiet";
        default:                return "normal";
    }
}

// D6b auto-router: turn the learned verdict into an actionable render mode.
// Given a command's accumulated stats, decide whether `icmg run` should
// automatically apply a tighter mode. Returns None unless the command is
// classified Noisy (repeated + large + mostly-filtered evidence), then Nano
// for build/test diagnostics or Gist otherwise. Pure -> the caller gates it
// behind an opt-in env flag so default behaviour is unchanged.
enum class AutoRoute { None, Nano, Gist };

inline AutoRoute autoRouteMode(const CmdStat& s, const LearnConfig& cfg = {}) {
    LearnResult r = analyzeOne(s, cfg);
    if (r.cls != LearnClass::Noisy) return AutoRoute::None;
    return learnLooksLikeBuild(s.command) ? AutoRoute::Nano : AutoRoute::Gist;
}

inline const char* autoRouteName(AutoRoute a) {
    switch (a) {
        case AutoRoute::Nano: return "nano";
        case AutoRoute::Gist: return "gist";
        default:              return "none";
    }
}

} // namespace icmg::tkil
