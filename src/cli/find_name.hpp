#pragma once
// Filename fuzzy locator for `icmg find --name <partial>`.
//
// Unlike content ranking (find_slices.hpp), this matches ONLY the file PATH /
// basename -- no file body is read, so it is ~10-50x faster when you just want
// to locate a file by (partial) name. Pure ranking here is unit-testable; the
// filesystem walk lives in the command.
//
// Scoring (higher = better, 0 = no match, dropped):
//   - exact basename match (with or without extension)  -> huge
//   - query is a substring of the basename              -> large (+ compactness)
//   - basename starts with the query                    -> prefix bonus
//   - query is a substring of the full relative path     -> medium
//   - all query chars appear in order in basename (fuzzy)-> small
// Shorter basenames win ties (tighter match). Case-insensitive.
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace icmg::cli {

struct NameHit {
    std::string path;
    double score = 0.0;
};

namespace detail {

inline std::string toLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

inline std::string stem(const std::string& base) {
    size_t dot = base.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? base : base.substr(0, dot);
}

// True if every char of needle appears in haystack in order (subsequence).
inline bool isSubsequence(const std::string& needle, const std::string& hay) {
    if (needle.empty()) return false;
    size_t j = 0;
    for (char c : hay) {
        if (j < needle.size() && c == needle[j]) ++j;
        if (j == needle.size()) return true;
    }
    return j == needle.size();
}

}  // namespace detail

// Score one path against a (already-lowercased) query. 0 = no match.
inline double scoreFilename(const std::string& path, const std::string& qLower) {
    using namespace detail;
    if (qLower.empty()) return 0.0;
    std::string lpath = toLower(path);
    std::string base  = baseName(lpath);
    std::string st    = stem(base);

    double score = 0.0;
    if (st == qLower || base == qLower) {
        score = 1000.0;                                    // exact basename/stem
    } else if (base.rfind(qLower, 0) == 0) {
        score = 600.0;                                     // basename prefix
    } else if (base.find(qLower) != std::string::npos) {
        score = 400.0;                                     // substring of basename
    } else if (lpath.find(qLower) != std::string::npos) {
        score = 200.0;                                     // substring of full path
    } else if (isSubsequence(qLower, base)) {
        score = 100.0;                                     // fuzzy subsequence
    } else {
        return 0.0;
    }
    // Compactness: shorter basename = tighter match -> small additive bonus
    // (kept < tier gap so it never reorders across tiers).
    score += 50.0 / (1.0 + (double)base.size());
    return score;
}

// Rank paths by filename relevance to query; return top maxResults (score>0).
inline std::vector<NameHit> rankFilenames(const std::vector<std::string>& paths,
                                          const std::string& query,
                                          int maxResults = 20) {
    std::string qLower = detail::toLower(query);
    // strip surrounding whitespace
    size_t a = qLower.find_first_not_of(" \t");
    size_t b = qLower.find_last_not_of(" \t");
    if (a == std::string::npos) return {};
    qLower = qLower.substr(a, b - a + 1);

    std::vector<NameHit> out;
    for (const auto& p : paths) {
        double s = scoreFilename(p, qLower);
        if (s > 0.0) out.push_back({p, s});
    }
    std::sort(out.begin(), out.end(), [](const NameHit& x, const NameHit& y) {
        if (x.score != y.score) return x.score > y.score;
        return x.path < y.path;  // stable, deterministic
    });
    if ((int)out.size() > maxResults) out.resize(maxResults);
    return out;
}

}  // namespace icmg::cli
