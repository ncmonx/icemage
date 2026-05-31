#pragma once
// v1.79.1 T6: opt-in LLM atomizer (parse + fallback). Pure, testable.
// The actual warm-pool call lives in atom_store.cpp (worker-only, gated
// ICMG_ATOMIZE_LLM=1). Here: parse one-fact-per-line model output + fall
// back to the heuristic split when the model returns nothing usable.
#include "atom_split.hpp"
#include <string>
#include <vector>

namespace icmg::imem {

// Parse "- one fact per line" model output into atoms. Trims, drops empties +
// leading bullet/dash markers.
inline std::vector<std::string> parseLlmAtoms(const std::string& out) {
    std::vector<std::string> v;
    size_t i = 0, n = out.size();
    while (i < n) {
        size_t e = out.find('\n', i);
        if (e == std::string::npos) e = n;
        std::string line = out.substr(i, e - i);
        size_t a = line.find_first_not_of(" \t\r-*");
        if (a != std::string::npos) {
            size_t b = line.find_last_not_of(" \t\r");
            std::string s = line.substr(a, b - a + 1);
            if (s.size() >= 2) v.push_back(std::move(s));
        }
        i = e + 1;
    }
    return v;
}

// If model_output parses to >=1 atom, use it; else heuristic fallback on src.
inline std::vector<std::string> llmAtomizeOrFallback(const std::string& src,
                                                     const std::string& model_output) {
    auto v = parseLlmAtoms(model_output);
    if (!v.empty()) return v;
    return atomSplit(src);
}

} // namespace icmg::imem
