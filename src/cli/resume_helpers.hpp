// 2026-06-07: pure formatter for `icmg wake-up --resume` (luna idea "context --resume":
// wake briefing + persona identity + moments in one shot). No DB/IO — caller passes rows.
#pragma once
#include <string>
#include <vector>
#include <utility>

namespace icmg::cli {

// Format the persona/continuity section appended to a wake-up briefing on --resume.
// identity = (key,value) from persona _identity zone; moments = recent _moments titles.
// Returns "" when both empty (so --resume on a fresh install adds nothing).
inline std::string resumeSection(const std::vector<std::pair<std::string,std::string>>& identity,
                                 const std::vector<std::string>& moments) {
    if (identity.empty() && moments.empty()) return "";
    std::string s = "\n## Persona / resume\n";
    for (const auto& kv : identity) s += "- " + kv.first + ": " + kv.second + "\n";
    if (!moments.empty()) {
        s += "- moments: ";
        for (size_t i = 0; i < moments.size(); ++i) { if (i) s += "; "; s += moments[i]; }
        s += "\n";
    }
    return s;
}

} // namespace icmg::cli
