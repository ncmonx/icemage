#pragma once
// v2.0.0 C2: cross-turn dedup. Before the governor injects a context slice, skip it if a
// near-duplicate is already in the window (recently-injected slices). Word-set Jaccard,
// pure + deterministic -> unit-testable. Pinned content should bypass this (caller's choice).
#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
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

// Cross-turn variant: window is a newline-delimited file of recently-injected
// slices that survives across UserPromptSubmit turns (and process restarts).
// Returns true if `slice` is a near-duplicate of a window entry (caller SKIPS it).
// On a non-dup, appends `slice` to the window file (capped to last `maxWindow`
// lines, FIFO) and returns false (caller EMITS it). Empty/whitespace slices are
// never dups and never recorded. The ID-based per-node dedup (session-injected-ids)
// catches exact same-node re-injection; this catches different-id near-identical
// content that the ID gate misses.
inline bool dedupAgainstWindowFile(const std::string& slice,
                                   const std::string& windowPath,
                                   double threshold,
                                   size_t maxWindow = 64) {
    if (wordSet(slice).empty()) return false;
    std::deque<std::string> window;
    {
        std::ifstream in(windowPath);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) window.push_back(line);
        }
    }
    std::vector<std::string> win(window.begin(), window.end());
    if (isDuplicateInWindow(slice, win, threshold)) return true;
    // record: append, FIFO-cap, rewrite (window is small -> cheap).
    window.push_back(slice);
    while (window.size() > maxWindow) window.pop_front();
    std::ofstream out(windowPath, std::ios::trunc);
    for (const auto& w : window) out << w << "\n";
    return false;
}

}  // namespace icmg::core
