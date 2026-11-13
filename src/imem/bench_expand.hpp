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
//
//   adjacentExpandHead sentence-window pool expansion (2026-11-12): each of the
//                      first `seeds` ranked docs is followed in the rerank
//                      head by its same-session neighbour turns, so the
//                      cross-encoder can judge the answer turn that sits next
//                      to a question-shaped hit.
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

constexpr int kAdjacentMax   = 3;    // --adjacent W range: +-1..3 turns
constexpr int kAdjacentSeeds = 10;   // default seeds that pull neighbours in
constexpr int kAdjacentSeedsMax = 50;

struct AdjacentHead {
    std::vector<std::string> ids;   // every ranked id once (+ pulled-in neighbours)
    int head = 0;                   // first `head` ids form the rerank head
};

// ranked: current order. adj: id -> neighbour ids in preference order. The
// first min(head, n) ranked ids form the head in order; each of the first
// `seeds` is followed by its not-yet-placed neighbours (the head grows by
// that many). Remaining ranked ids follow in order, minus those pulled in.
AdjacentHead adjacentExpandHead(const std::vector<std::string>& ranked,
                                const std::map<std::string, std::vector<std::string>>& adj,
                                int seeds, int head);

}  // namespace icmg::imem
