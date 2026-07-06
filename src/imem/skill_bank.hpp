#pragma once
// Trajectory -> skill-bank distillation (feature #7).
//
// Deterministic distillation of successful command trajectories into a bounded,
// stable-size reusable skill bank with causal attribution ("what worked" =
// success rate). Source data = the `verifications` table (command + exit_code
// history) joined with usage frequency; this header holds the PURE ranking so
// it is unit-testable without a DB. No LLM.
//
// Extends icmg learn / verifications rather than parallel-building: learn mines
// raw command frequency; this mines proven-successful trajectories.

#include <string>
#include <vector>
#include <algorithm>

namespace icmg::imem {

// Aggregated per-command trajectory: how often it ran and how often it worked.
struct TrajectoryStat {
    std::string command;
    int         uses      = 0;   // total invocations recorded
    int         successes = 0;   // invocations with exit_code == 0
};

// A distilled skill = a command proven to work, with causal attribution.
struct Skill {
    std::string command;
    int         uses         = 0;
    double      success_rate = 0.0;  // successes / uses (the "what worked" signal)
};

// Distill a bounded, ranked skill bank from trajectory stats.
//   maxSkills : hard cap -> stable bank size
//   minFreq   : ignore commands used fewer than this many times (noise floor)
//   minRate   : ignore commands whose success rate is below this (didn't work)
// Ranking: success_rate desc, then uses desc, then command asc (stable/determin.).
inline std::vector<Skill> distillSkills(const std::vector<TrajectoryStat>& stats,
                                        int maxSkills = 20,
                                        int minFreq   = 2,
                                        double minRate = 0.5) {
    std::vector<Skill> out;
    for (const auto& t : stats) {
        if (t.uses < minFreq) continue;
        if (t.uses <= 0) continue;
        double rate = (double)t.successes / (double)t.uses;
        if (rate < minRate) continue;
        out.push_back({t.command, t.uses, rate});
    }
    std::sort(out.begin(), out.end(), [](const Skill& a, const Skill& b) {
        if (a.success_rate != b.success_rate) return a.success_rate > b.success_rate;
        if (a.uses != b.uses)                 return a.uses > b.uses;
        return a.command < b.command;
    });
    if ((int)out.size() > maxSkills) out.resize(maxSkills);
    return out;
}

} // namespace icmg::imem
