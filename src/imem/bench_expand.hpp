// 2026-11-11: query-time expansion / selection stages for bench-recall
// --dataset (research: docs/plans/2026-11-11-token-memory-research.md).
// Pure functions, no DB, no model -- unit-testable, portable to live recall.
//
//   rm3ExpansionTerms  RM3 pseudo-relevance feedback (Lavrenko & Croft 2001):
//                      w(t) = sum_d  P(d) * tf(t,d) / |d|,  P(d) = score(d) / sum
//                      over the top feedback docs; query terms excluded so the
//                      caller can run them as a second lexical query and
//                      interpolate scores: s = (1-beta) * s_q + beta * s_exp.
//
//   coverSelect        coverage-greedy evidence chain (after Chain on Demand,
//                      arXiv 2611.02480): over the head, repeatedly pick
//                      argmax rel(d) + lam * newlyCovered(q,d)/|q|
//                                    - mu  * maxJaccard(d, picked)
//                      so multi-hop questions get one doc per missing fact.
//
//   rerankGateSkip     adaptive rerank gate (query-performance prediction):
//                      skip the cross-encoder when (s1-s2)/s1 >= tau AND the
//                      top-1 doc contains every query term.
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace icmg::imem {

constexpr int    kPrfFeedbackDocs = 10;  // top-K lexical hits used as feedback
constexpr int    kPrfTerms        = 10;  // expansion terms kept
constexpr size_t kPrfMinTermLen   = 3;   // drop "ab", "of"-style fragments

struct PrfTerm {
    std::string term;
    double      weight = 0.0;
};

// docTokens[i] / docScores[i] describe feedback doc i (scores >= 0, any scale).
// Returns <= numTerms terms by weight desc (ties: alphabetical). Size mismatch,
// no docs, or all-zero scores -> {}.
std::vector<PrfTerm> rm3ExpansionTerms(const std::vector<std::string>& queryTokens,
                                       const std::vector<std::vector<std::string>>& docTokens,
                                       const std::vector<double>& docScores,
                                       int numTerms);

constexpr int    kCoverPool     = 30;   // greedy head size (rest keeps its order)
constexpr double kCoverMuPerLam = 1.0;  // redundancy weight mu = lam * this

// cand: (id, score) sorted desc. docTokens: id -> tokens (missing id = empty).
// rel = min-max of the head's scores. Returns every id: the greedy-picked
// first `budget`, then the remaining ones in their original order.
std::vector<std::string> coverSelect(const std::vector<std::pair<std::string, double>>& cand,
                                     const std::map<std::string, std::vector<std::string>>& docTokens,
                                     const std::vector<std::string>& queryTokens,
                                     double lam, double mu, int budget);

// tau <= 0 (gate off), s1 <= 0 or an empty query -> false (always rerank).
bool rerankGateSkip(double s1, double s2,
                    const std::vector<std::string>& top1Tokens,
                    const std::vector<std::string>& queryTokens, double tau);

}  // namespace icmg::imem
