// Phase 66 + Phase 68: line-set-diff for `pack --diff`.
// Header-only so tests can include directly without linking icmg_lib.

#pragma once
#include <sstream>
#include <string>
#include <unordered_set>

namespace icmg::cli {

inline std::string computePackDelta(const std::string& prev,
                                     const std::string& cur) {
    std::unordered_set<std::string> prev_lines;
    std::istringstream ps(prev);
    std::string line;
    while (std::getline(ps, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) prev_lines.insert(line);
    }
    std::ostringstream out;
    std::istringstream cs(cur);
    while (std::getline(cs, line)) {
        std::string trimmed = line;
        while (!trimmed.empty() && trimmed.back() == '\r') trimmed.pop_back();
        if (trimmed.empty() || !prev_lines.count(trimmed)) {
            out << line << "\n";
        }
    }
    return out.str();
}

} // namespace icmg::cli
