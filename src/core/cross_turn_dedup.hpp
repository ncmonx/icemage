#pragma once
// v2.0.0 C2: cross-turn dedup. Before the governor injects a context slice, skip it if a
// near-duplicate is already in the window (recently-injected slices). Word-set Jaccard,
// pure + deterministic -> unit-testable. Pinned content should bypass this (caller's choice).
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace icmg::core {

// Lowercased word set (tokens of length >= 2; punctuation splits).
inline std::set<std::string> wordSet(const std::string& s) {
    std::set<std::string> out;
    std::string cur;
    auto flush = [&]() { if (cur.size() >= 2) out.insert(cur); cur.clear(); };
    for (char ch : s) {
        unsigned char c = (unsigned char)ch;
        if (std::isalnum(c)) cur += (char)std::tolower(c);
        else flush();
    }
    flush();
    return out;
}

inline double jaccardSets(const std::set<std::string>& a, const std::set<std::string>& b) {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;
    size_t inter = 0;
    for (const auto& w : a) if (b.count(w)) ++inter;
    size_t uni = a.size() + b.size() - inter;
    return uni ? (double)inter / (double)uni : 0.0;
}

// True if `slice` is a near-duplicate (Jaccard >= threshold) of any window entry.
inline bool isDuplicateInWindow(const std::string& slice,
                                const std::vector<std::string>& window,
                                double threshold) {
    auto a = wordSet(slice);
    for (const auto& w : window) {
        if (jaccardSets(a, wordSet(w)) >= threshold) return true;
    }
    return false;
}

// Keep slices not already in the window AND not near-duplicates of an earlier kept slice.
inline std::vector<std::string> dedupeAgainstWindow(const std::vector<std::string>& slices,
                                                    const std::vector<std::string>& window,
                                                    double threshold) {
    std::vector<std::string> kept;
    for (const auto& s : slices) {
        if (isDuplicateInWindow(s, window, threshold)) continue;
        if (isDuplicateInWindow(s, kept, threshold)) continue;
        kept.push_back(s);
    }
    return kept;
}

}  // namespace icmg::core
