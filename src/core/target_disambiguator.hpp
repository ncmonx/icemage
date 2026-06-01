#pragma once
#include <string>
#include <vector>
namespace icmg::core {

struct DisambigCandidate {
    std::string name;    // symbol or basename
    std::string path;    // file path (may be empty)
    double      score;   // 0.0 .. 1.0
};

// Score candidate names against user prompt tokens.
// Algorithm: char-trigram Jaccard between best-matching prompt token and
// each candidate name, plus +0.10 suffix-overlap bonus when >=2 candidates
// share a >=3-char suffix.
// Returns candidates with score >= threshold, sorted desc by score.
std::vector<DisambigCandidate> disambiguateTargets(
    const std::string& user_prompt,
    const std::vector<std::pair<std::string, std::string>>& candidates,  // {name, path}
    double threshold = 0.80);

// Exposed for testing + hook reuse.
std::vector<std::string> tokenize(const std::string& text);
double trigramJaccard(const std::string& a, const std::string& b);

} // namespace icmg::core
