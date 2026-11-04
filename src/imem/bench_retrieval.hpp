// 2026-10-31: experimental retrieval stages for bench-recall --dataset.
//
// Pure functions (no DB, no embedder) so every stage is unit-testable and can
// later be ported to live recall once the benchmark proves it. Stages:
//   rrfFuse          Reciprocal Rank Fusion of several ranked lists (scale-free;
//                    replaces the alpha-weighted BM25+cosine blend).
//   chunkWords       split a long doc into overlapping word windows so a
//                    128-token embedder sees all of it (multi-vector dense).
//   firstPersonView  the user's own lines of a chat session -- where personal
//                    facts and preferences live.
//   parseTemporalHint  "last week" / "3 days ago" / "in May" -> date window.
//   groupPrior       lift a turn by its session's best score (two-tier recall).
//   mmrSelect        Maximal Marginal Relevance diversification (multi-hop).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace icmg::imem {

// Standard RRF constant (Cormack et al. 2009).
constexpr double kRrfK = 60.0;

// score(id) = sum over lists of 1 / (k + rank), rank 1-based. Returns ids by
// fused score desc (ties: first-seen order).
std::vector<std::pair<std::string, double>>
rrfFuse(const std::vector<std::vector<std::string>>& rankedLists, double k = kRrfK);

// Overlapping windows of at most maxWords words, stride maxWords - overlap.
// Empty text -> no chunks; text shorter than maxWords -> one chunk.
std::vector<std::string> chunkWords(const std::string& text, int maxWords, int overlap);

// Lines of a "role: content" transcript whose role is `user`, prefix stripped.
std::string firstPersonView(const std::string& transcript);

struct TemporalWindow {
    bool    found = false;
    int64_t lo = 0, hi = 0;  // inclusive epoch range
};

// Relative/absolute time phrase in the question, resolved against asOf.
TemporalWindow parseTemporalHint(const std::string& question, int64_t asOf);

// score' = score + beta * max(score of docs in the same group).
std::map<std::string, double>
groupPrior(const std::map<std::string, double>& scores,
           const std::map<std::string, std::string>& groupOf, double beta);

// Greedy MMR over candidates (already sorted by relevance desc).
// lambda=1 -> pure relevance. sim(a,b) supplied as a cosine matrix lookup.
std::vector<std::string>
mmrSelect(const std::vector<std::pair<std::string, double>>& cand,
          const std::map<std::string, std::vector<float>>& vecs,
          double lambda, int k);

// Evidence-id -> its session: "sid#3" -> "sid", "D3:5" -> "D3", else itself.
std::string benchGroupOf(const std::string& docId);

// "2023-05-08 (May 2023)" -- date label prepended by --enrich so lexical and
// dense retrieval can both see when something was said. "" for epoch <= 0.
std::string benchDateLabel(int64_t epoch);

// Re-order the first scores.size() ids of `ranked` by score desc (stable);
// tail untouched. Mismatched / empty scores -> ranked returned unchanged.
std::vector<std::string>
rerankHead(const std::vector<std::string>& ranked, const std::vector<float>& scores, int n);

// Chunked rerank (2026-11-01): indices of the <= m chunks sharing the most
// query tokens, returned in original order (cheap passage pre-select so a long
// doc costs m cross-encoder pairs, not all of its windows).
std::vector<size_t> topOverlapChunks(const std::string& query,
                                     const std::vector<std::string>& chunks, int m);

// Fold passage scores into per-doc scores: doc i = max over passages with
// owner[p] == i; docs without passages get -1e30. Size mismatch -> {}.
std::vector<float> maxPerOwner(const std::vector<float>& passageScores,
                               const std::vector<size_t>& owner, size_t nDocs);

}  // namespace icmg::imem
