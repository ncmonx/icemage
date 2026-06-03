#pragma once
// v2.0.0 C1+C3: injection governor — pure, model-free working-set selection +
// lost-in-the-middle ordering. selectWorkingSet keeps the highest (priority x
// relevance) candidates within a token budget; pinned candidates always survive
// even when over budget. orderUShaped reorders the kept set so the highest-relevance
// items sit at the extrema (front+back) and the lowest in the middle, mitigating
// RoPE U-shaped attention decay. No I/O, no DB, no model -> fully unit-testable.
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace icmg::core {

struct Source {
    std::string id;
    std::string text;
    int tokens = 0;
    double relevance = 0.0;  // [0,1] BM25/recency blend supplied by caller
    int priority = 1;        // higher = more important (pinned/decisions > filler)
    bool pinned = false;     // pinned always survives the budget cut
};

struct WorkingSet {
    std::vector<Source> items;  // in original input order
    int totalTokens = 0;
};

// score = priority * relevance (priority dominates; relevance breaks within-tier).
inline double wsScore(const Source& s) { return (double)s.priority * s.relevance; }

// Greedy budget fill: pinned first (unconditional), then highest-score candidates
// while they fit. Kept items emitted in original input order.
inline WorkingSet selectWorkingSet(const std::vector<Source>& candidates, int budgetTokens) {
    const size_t n = candidates.size();
    std::vector<bool> keep(n, false);
    int used = 0;

    // Pass 1: pinned survive unconditionally.
    for (size_t i = 0; i < n; ++i) {
        if (candidates[i].pinned) { keep[i] = true; used += candidates[i].tokens; }
    }

    // Pass 2: rank remaining by score desc (stable: ties keep earlier index).
    std::vector<size_t> order;
    for (size_t i = 0; i < n; ++i) if (!candidates[i].pinned) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return wsScore(candidates[a]) > wsScore(candidates[b]);
    });
    for (size_t i : order) {
        if (used + candidates[i].tokens <= budgetTokens) {
            keep[i] = true; used += candidates[i].tokens;
        }
    }

    WorkingSet ws;
    for (size_t i = 0; i < n; ++i) {
        if (keep[i]) { ws.items.push_back(candidates[i]); ws.totalTokens += candidates[i].tokens; }
    }
    return ws;
}

// Reorder by relevance into a U-shape: highest-relevance items at the extrema
// (front and back), lowest in the middle. Mitigates "lost-in-the-middle" RoPE decay
// where models under-attend mid-context. Pure; stable for equal relevance.
inline std::vector<Source> orderUShaped(std::vector<Source> items) {
    const size_t n = items.size();
    if (n <= 1) return items;

    // Rank by relevance desc (stable).
    std::vector<size_t> rank(n);
    std::iota(rank.begin(), rank.end(), (size_t)0);
    std::stable_sort(rank.begin(), rank.end(), [&](size_t a, size_t b) {
        return items[a].relevance > items[b].relevance;
    });

    // Place rank[0] at front, rank[1] at back, rank[2] at front+1, rank[3] at back-1, ...
    std::vector<Source> out(n);
    size_t lo = 0, hi = n - 1;
    bool toFront = true;
    for (size_t r = 0; r < n; ++r) {
        if (toFront) out[lo++] = items[rank[r]];
        else         out[hi--] = items[rank[r]];
        toFront = !toFront;
    }
    return out;
}

}  // namespace icmg::core
