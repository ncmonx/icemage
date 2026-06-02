#pragma once
// v2.0.0 TE2: salience compression core (LLMLingua-style coarse-to-fine), pure +
// model-free so it is unit-testable. Keep the highest-scoring spans within a char
// budget, preserving original order. The per-span score is pluggable: infoScore()
// is the heuristic default; a llama-logprob (true perplexity) scorer can replace it
// later without touching this selection logic.
#include <algorithm>
#include <cctype>
#include <numeric>
#include <string>
#include <vector>

namespace icmg::core {

// Heuristic information score of a line/span in [0,1]: word- and identifier-density.
// Boilerplate (rules of dashes, pure punctuation, blank) scores ~0; identifier-rich
// code/prose scores high.
inline double infoScore(const std::string& s) {
    size_t wordTokens = 0, richTokens = 0, cur = 0;
    bool curHasAlpha = false;
    auto flush = [&]() {
        if (cur >= 2 && curHasAlpha) {
            ++wordTokens;
            if (cur >= 4) ++richTokens;
        }
        cur = 0; curHasAlpha = false;
    };
    for (char ch : s) {
        unsigned char c = (unsigned char)ch;
        if (std::isalnum(c) || c == '_') {
            ++cur;
            if (std::isalpha(c)) curHasAlpha = true;
        } else {
            flush();
        }
    }
    flush();
    double raw = (double)wordTokens * 0.15 + (double)richTokens * 0.25;
    if (raw < 0.0) raw = 0.0;
    if (raw > 1.0) raw = 1.0;
    return raw;
}

// Keep the highest-score spans whose cumulative size (with separators) fits budget,
// emitted in ORIGINAL order. Never returns empty when there is input — at least the
// single top-scoring span survives (so a 0/under budget still yields signal).
inline std::string selectByBudget(const std::vector<std::string>& spans,
                                  const std::vector<double>& scores,
                                  size_t budgetChars,
                                  const std::string& sep = "\n") {
    const size_t n = spans.size();
    if (n == 0) return "";

    // Rank indices by score desc; ties keep earlier index (stable).
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), (size_t)0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        double sa = a < scores.size() ? scores[a] : 0.0;
        double sb = b < scores.size() ? scores[b] : 0.0;
        return sa > sb;
    });

    std::vector<bool> keep(n, false);
    size_t used = 0;
    for (size_t rank = 0; rank < order.size(); ++rank) {
        size_t i = order[rank];
        size_t add = spans[i].size() + (used > 0 ? sep.size() : 0);
        if (rank == 0) { keep[i] = true; used += spans[i].size(); continue; }  // top-1 always
        if (used + add <= budgetChars) { keep[i] = true; used += add; }
    }

    std::string out;
    for (size_t i = 0; i < n; ++i) {
        if (!keep[i]) continue;
        if (!out.empty()) out += sep;
        out += spans[i];
    }
    return out;
}

}  // namespace icmg::core
