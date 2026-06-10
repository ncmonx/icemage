#pragma once
// Intent slice: return the lines of a file that are RELEVANT to a natural-
// language intent, instead of the whole file or a guessed line range.
//
// Why: when the agent must read a big file to find one block, it either reads
// the whole thing or GUESSES `--lines A-B`. Both waste tokens. `icmg context
// <file> --for "<intent>"` scores each line against the intent terms, builds a
// context window around every hit, merges overlaps, and returns the top windows
// (in reading order) -- ~30 lines instead of ~500, no line-number guessing.
//
// v1 is pure line-window scoring (no graph dependency) so it is unit-testable
// in isolation. Symbol-boundary snapping is a later enhancement.
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace icmg::core {

struct LineRange { int start; int end; };  // 1-based, inclusive

// Tokenize the intent: lowercase alnum runs, drop length<2 and common stopwords.
inline std::vector<std::string> intentTokens(const std::string& q) {
    static const std::unordered_set<std::string> stop = {
        "the","and","or","for","is","of","to","in","a","an","with",
        "this","that","on","at","by","it","as","be"};
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.size() >= 2 && !stop.count(cur)) out.push_back(cur);
        cur.clear();
    };
    for (char c : q) {
        if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
        else flush();
    }
    flush();
    return out;
}

// Score lines against the intent and return the most-relevant windows.
//  contextLines  : lines of padding on each side of a hit
//  maxRanges     : keep at most this many windows (highest-scoring first)
//  maxTotalLines : soft cap on total lines emitted across all windows
// Returns ranges in reading order (ascending start). Empty if no hit.
inline std::vector<LineRange> intentSliceRanges(const std::string& body,
                                                const std::string& query,
                                                int contextLines = 6,
                                                int maxRanges = 4,
                                                int maxTotalLines = 80) {
    std::vector<std::string> terms = intentTokens(query);
    if (terms.empty()) return {};

    std::vector<std::string> lines;
    { std::istringstream is(body); std::string ln;
      while (std::getline(is, ln)) lines.push_back(ln); }
    const int N = (int)lines.size();
    if (N == 0) return {};

    // Score = # distinct intent terms appearing in the line (case-insensitive).
    struct Win { int start, end, score; };
    std::vector<Win> wins;
    for (int i = 0; i < N; ++i) {
        std::string low = lines[i];
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        int s = 0;
        for (const auto& t : terms) if (low.find(t) != std::string::npos) ++s;
        if (s <= 0) continue;
        int a = std::max(1, (i + 1) - contextLines);   // 1-based
        int b = std::min(N, (i + 1) + contextLines);
        wins.push_back({a, b, s});
    }
    if (wins.empty()) return {};

    // Merge overlapping / adjacent windows (sum their scores).
    std::sort(wins.begin(), wins.end(),
              [](const Win& x, const Win& y) { return x.start < y.start; });
    std::vector<Win> merged;
    for (const auto& w : wins) {
        if (!merged.empty() && w.start <= merged.back().end + 1) {
            merged.back().end = std::max(merged.back().end, w.end);
            merged.back().score += w.score;
        } else {
            merged.push_back(w);
        }
    }

    // Keep the highest-scoring windows within the line budget.
    std::sort(merged.begin(), merged.end(), [](const Win& x, const Win& y) {
        if (x.score != y.score) return x.score > y.score;
        return x.start < y.start;
    });
    std::vector<Win> kept;
    int total = 0;
    for (const auto& w : merged) {
        if ((int)kept.size() >= maxRanges) break;
        int len = w.end - w.start + 1;
        if (!kept.empty() && total + len > maxTotalLines) continue;
        kept.push_back(w);
        total += len;
    }

    // Emit in reading order.
    std::sort(kept.begin(), kept.end(),
              [](const Win& x, const Win& y) { return x.start < y.start; });
    std::vector<LineRange> out;
    out.reserve(kept.size());
    for (const auto& w : kept) out.push_back({w.start, w.end});
    return out;
}

}  // namespace icmg::core
