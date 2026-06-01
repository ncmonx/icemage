#include "target_disambiguator.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace icmg::core {

// ---- tokenize --------------------------------------------------------------

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string cur;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '_') {
            cur += (char)std::tolower(c);
        } else {
            if (cur.size() >= 2) tokens.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 2) tokens.push_back(cur);
    return tokens;
}

// ---- trigramJaccard --------------------------------------------------------

double trigramJaccard(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return 0.0;

    // Build trigram sets.
    auto buildTrigrams = [](const std::string& s) -> std::set<std::string> {
        std::set<std::string> out;
        if (s.size() < 3) {
            // Pad short strings with spaces to form one trigram.
            std::string padded = s;
            while (padded.size() < 3) padded += ' ';
            out.insert(padded.substr(0, 3));
            return out;
        }
        for (size_t i = 0; i + 2 < s.size(); ++i)
            out.insert(s.substr(i, 3));
        return out;
    };

    auto ta = buildTrigrams(a);
    auto tb = buildTrigrams(b);

    // Intersection size.
    int isect = 0;
    for (auto& t : ta)
        if (tb.count(t)) ++isect;

    // Union size.
    std::set<std::string> un(ta);
    un.insert(tb.begin(), tb.end());

    if (un.empty()) return 0.0;
    return (double)isect / (double)un.size();
}

// ---- suffix bonus ----------------------------------------------------------

// Returns the longest common suffix (>=3 chars) shared between any two candidate names.
// Used to apply a +0.10 bonus when two or more names share a suffix.
static std::string longestSharedSuffix(const std::vector<std::pair<std::string,std::string>>& cands) {
    if (cands.size() < 2) return "";
    // Gather all name pairs.
    for (size_t i = 0; i + 1 < cands.size(); ++i) {
        for (size_t j = i + 1; j < cands.size(); ++j) {
            const std::string& a = cands[i].first;
            const std::string& b = cands[j].first;
            // Build common suffix.
            size_t k = 0;
            while (k < a.size() && k < b.size() &&
                   a[a.size()-1-k] == b[b.size()-1-k]) ++k;
            if (k >= 3) {
                return a.substr(a.size() - k);
            }
        }
    }
    return "";
}

// ---- disambiguateTargets ---------------------------------------------------

std::vector<DisambigCandidate> disambiguateTargets(
    const std::string& user_prompt,
    const std::vector<std::pair<std::string, std::string>>& candidates,
    double threshold)
{
    if (candidates.empty()) return {};

    std::vector<std::string> prompt_tokens = tokenize(user_prompt);
    if (prompt_tokens.empty()) return {};

    // Determine shared suffix for bonus.
    std::string shared_suffix = longestSharedSuffix(candidates);
    bool has_suffix_bonus = !shared_suffix.empty();

    // Score each candidate.
    std::vector<DisambigCandidate> results;
    for (auto& [name, path] : candidates) {
        // Best trigram match between any prompt token and the candidate name.
        double best = 0.0;
        for (auto& tok : prompt_tokens) {
            double s = trigramJaccard(tok, name);
            if (s > best) best = s;
        }

        // Suffix bonus: if >=2 candidates share a >=3-char suffix AND this
        // candidate ends with that suffix, add +0.10.
        double bonus = 0.0;
        if (has_suffix_bonus && name.size() >= shared_suffix.size()) {
            std::string tail = name.substr(name.size() - shared_suffix.size());
            if (tail == shared_suffix) bonus = 0.10;
        }

        double score = std::min(1.0, best + bonus);
        if (score >= threshold) {
            results.push_back({name, path, score});
        }
    }

    // Sort descending by score.
    std::sort(results.begin(), results.end(),
              [](const DisambigCandidate& a, const DisambigCandidate& b){
                  return a.score > b.score;
              });

    return results;
}

} // namespace icmg::core
