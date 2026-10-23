// v2.26.0: patch-risk scoring for `icmg test-select --risk`.
//
// Deterministic risk grade per changed file so an agent can size its test
// run instead of guessing (SWE-VERIFIER, arXiv 2610.30284: selective
// verification beats verify-everything on cost). HIGH = run the full suite.
#pragma once
#include <set>
#include <sstream>
#include <string>

namespace icmg::cli {

// impact_count = reverse-impact closure size for the file.
// Shared low-level headers (src/core/*.hpp) are HIGH regardless of the
// measured closure -- the graph may understate header blast radius.
inline std::string riskLevel(size_t impact_count, const std::string& path) {
    auto endsWith = [&](const char* suf) {
        std::string s(suf);
        return path.size() >= s.size() &&
               path.compare(path.size() - s.size(), s.size(), s) == 0;
    };
    bool header = endsWith(".hpp") || endsWith(".h");
    bool shared_core = header &&
        (path.find("src/core/") != std::string::npos ||
         path.find("src\\core\\") != std::string::npos);
    if (shared_core || impact_count >= 25) return "HIGH";
    if (impact_count >= 8)                 return "MED";
    return "LOW";
}

// {"tests/cli/test_a.cpp", "tests/x/test_b.cpp"} -> "(test_a|test_b)"
// std::set iteration is sorted -> deterministic regex.
inline std::string ctestRegexFor(const std::set<std::string>& test_files) {
    std::ostringstream os;
    os << "(";
    size_t i = 0;
    for (const auto& t : test_files) {
        std::string stem = t;
        auto slash = stem.find_last_of("/\\");
        if (slash != std::string::npos) stem = stem.substr(slash + 1);
        auto dot = stem.rfind('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        if (stem.empty()) continue;
        if (i++) os << "|";
        os << stem;
    }
    os << ")";
    return os.str();
}

} // namespace icmg::cli
