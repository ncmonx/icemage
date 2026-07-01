#pragma once
// A3 (2026-07-01): pure surprisal->salience adapter for perplexity-based
// compression. LLMLingua-style self-information: a token's information content
// is its surprisal (-log p). A span's salience is the mean surprisal of its
// tokens -- predictable/boilerplate spans score low (droppable), surprising/
// informative spans score high (keep). No llama here: the real per-token
// surprisals come from LlamaRunner::tokenSurprisals(); this header only
// aggregates + normalizes so it plugs into selectByBudget() unchanged.
#include <cstddef>
#include <vector>
#include <algorithm>

namespace icmg::core {

// Aggregate per-token surprisals into one score per span. spanTokenCounts
// partitions the token stream in order: span i owns the next count[i] tokens.
// Score = mean surprisal of the span's tokens (0 for an empty span). Counts
// that run past the available surprisals are clamped (averages what's there).
inline std::vector<double> spanSalience(const std::vector<float>& surprisals,
                                        const std::vector<int>& spanTokenCounts) {
    std::vector<double> out;
    out.reserve(spanTokenCounts.size());
    size_t pos = 0;
    for (int c : spanTokenCounts) {
        if (c <= 0) { out.push_back(0.0); continue; }
        double sum = 0.0;
        size_t taken = 0;
        for (int k = 0; k < c && pos < surprisals.size(); ++k, ++pos) {
            sum += surprisals[pos];
            ++taken;
        }
        out.push_back(taken > 0 ? sum / (double)taken : 0.0);
    }
    return out;
}

// Min-max normalize scores to [0,1]. A constant vector (max==min) maps to a
// neutral 0.5 for every element (no span is privileged when all are equal).
inline std::vector<double> normalizeUnit(const std::vector<double>& scores) {
    std::vector<double> out(scores.size());
    if (scores.empty()) return out;
    double mn = scores[0], mx = scores[0];
    for (double v : scores) { mn = std::min(mn, v); mx = std::max(mx, v); }
    double range = mx - mn;
    for (size_t i = 0; i < scores.size(); ++i)
        out[i] = (range > 0.0) ? (scores[i] - mn) / range : 0.5;
    return out;
}

}  // namespace icmg::core
