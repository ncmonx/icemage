#pragma once
// One-shot multi-file intent search. Instead of the model doing a
// Read -> Grep -> Read chain across turns, `icmg find "<intent>"` ranks files by
// relevance to the intent and returns the top files each with only their
// relevant line windows -- the answer in a single turn. Reuses the per-file
// intent slicer (core/intent_slice.hpp). Pure ranking here is unit-testable;
// the filesystem walk lives in the command.
//
// Ranking uses IDF weighting: a rare term (e.g. an exact identifier like
// `modelContextWindow`) outweighs a common one (`model`, `registry`), so the
// file that actually defines/uses the thing wins over keyword-dense noise.
#include "../core/intent_slice.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>
#include <unordered_map>

namespace icmg::cli {

struct FileSlice {
    std::string file;
    std::vector<core::LineRange> ranges;
    double score = 0.0;   // IDF-weighted intent relevance (higher = better)
};

// Count non-overlapping occurrences of needle in haystack.
inline int countOccurrences(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return 0;
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos;
         p = hay.find(needle, p + needle.size())) ++n;
    return n;
}

// Rank (path, body) pairs by intent relevance; return the top maxFiles, each
// with its top windows in reading order. Files with no hit are dropped.
//
// Optional working-set recency boost: when `ageSec` is provided (relpath ->
// file age in seconds), a file's relevance score is multiplied by
//   1 + alpha * exp(-age / halflifeSec)
// so recently-edited files (the active working set) surface first for an
// ambiguous intent. The boost is BOUNDED (<= 1+alpha) and only scales files
// that already match (sc>0) -- it never injects an irrelevant file, and a
// much-more-relevant old file still outranks a barely-relevant fresh one.
inline std::vector<FileSlice> rankFileSlices(
        const std::vector<std::pair<std::string, std::string>>& files,
        const std::string& intent,
        int ctxLines = 4, int maxFiles = 5, int maxWindowsPerFile = 3,
        const std::unordered_map<std::string, double>* ageSec = nullptr,
        double halflifeSec = 86400.0, double alpha = 0.25) {
    auto terms = core::intentTokens(intent);
    std::vector<FileSlice> out;
    if (terms.empty() || files.empty()) return out;

    const int N = (int)files.size();
    // Lowercased bodies + document frequency per term.
    std::vector<std::string> low(N);
    std::vector<int> df(terms.size(), 0);
    for (int i = 0; i < N; ++i) {
        low[i] = files[i].second;
        for (auto& c : low[i]) c = (char)std::tolower((unsigned char)c);
        for (size_t t = 0; t < terms.size(); ++t)
            if (low[i].find(terms[t]) != std::string::npos) ++df[t];
    }
    // IDF weight per term (always positive; rare term -> larger).
    std::vector<double> w(terms.size());
    for (size_t t = 0; t < terms.size(); ++t)
        w[t] = std::log((N + 1.0) / (df[t] + 1.0)) + 1.0;

    for (int i = 0; i < N; ++i) {
        double sc = 0.0;
        for (size_t t = 0; t < terms.size(); ++t)
            sc += countOccurrences(low[i], terms[t]) * w[t];
        if (sc <= 0.0) continue;
        auto ranges = core::intentSliceRanges(files[i].second, intent, ctxLines,
                                              maxWindowsPerFile, 60);
        if (ranges.empty()) continue;
        // Working-set recency boost (bounded; only lifts already-matching files).
        if (ageSec) {
            auto it = ageSec->find(files[i].first);
            if (it != ageSec->end() && it->second >= 0.0)
                sc *= 1.0 + alpha * std::exp(-it->second / halflifeSec);
        }
        out.push_back({files[i].first, ranges, sc});
    }
    std::sort(out.begin(), out.end(), [](const FileSlice& a, const FileSlice& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.file < b.file;
    });
    if ((int)out.size() > maxFiles) out.resize(maxFiles);
    return out;
}

}  // namespace icmg::cli
