// v1.63 F8: pure, testable helpers for `icmg mine`. Header-only so tests
// include directly without the command's DB coupling.

#pragma once
#include "../imem/memory_node.hpp"
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace icmg::cli {

// Topic prefix before the first '-' or ':' (e.g. "decisions-db" -> "decisions").
inline std::string topicPrefix(const std::string& topic) {
    auto p = topic.find_first_of("-:");
    return (p == std::string::npos) ? topic : topic.substr(0, p);
}

// Deterministic fallback: rank topic prefixes by frequency, surface
// recurring ones (>=2) as candidate-rule hints. Suggestions only.
inline std::string heuristicMine(const std::vector<imem::MemoryNode>& nodes) {
    std::map<std::string, int> freq;
    for (const auto& n : nodes) freq[topicPrefix(n.topic)] += 1;
    std::vector<std::pair<std::string,int>> ranked(freq.begin(), freq.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<std::string,int>& a,
                 const std::pair<std::string,int>& b){ return a.second > b.second; });
    std::ostringstream out;
    out << "[mine: heuristic suggestions — review, nothing applied]\n";
    int shown = 0;
    for (auto& kv : ranked) {
        if (kv.second < 2) continue;
        out << "  - consider a rule capturing your recurring \"" << kv.first
            << "\" decisions (" << kv.second << " entries)\n";
        if (++shown >= 5) break;
    }
    if (shown == 0)
        out << "  (no recurring topic prefix yet — keep storing decisions)\n";
    return out.str();
}

}  // namespace icmg::cli
