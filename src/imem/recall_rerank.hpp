#pragma once
// 2026-11-01: opt-in cross-encoder rerank for LIVE recall (CLI --rerank N,
// MCP icmg_recall "rerank", hook env ICMG_RECALL_RERANK=N).
// LoCoMo10 bench: R_any@1 .263 (bm25) -> .480 (bm25 + rerank 20).
// Recall over-fetches max(limit, N) BM25/hybrid candidates, the cross-encoder
// reorders the first N, then the list is cut back to limit. No model / ONNX
// off -> BM25 order kept untouched (fail-open, never an error).
#include "memory_node.hpp"

#include <string>
#include <vector>

namespace icmg::imem {

constexpr int    kRecallRerankMax     = 100;   // cross-encoder cost is linear in N
constexpr size_t kRecallRerankDocCap  = 2048;  // chars of topic+content per pair
constexpr const char* kRecallRerankEnv = "ICMG_RECALL_RERANK";
constexpr int    kRecallRerankWorkers = 1;     // live: one ONNX session (model load dominates)
constexpr int    kRecallRerankChunkWords = 150; // passage size (bench-proven, LME + LoCoMo)
constexpr int    kRecallRerankPassages   = 2;   // best passages scored per memory (4 -> ~5s, 2 keeps hook latency down)

// "20" -> 20; clamps to kRecallRerankMax; junk / <= 0 -> 0 (off).
int parseRerankN(const std::string& s);

// N from env ICMG_RECALL_RERANK (hook path); 0 when unset.
int rerankNFromEnv();

// Text the cross-encoder sees for one memory: "topic: content", capped.
std::string rerankDocText(const MemoryNode& n);

// Passages the cross-encoder scores for one memory: 150-word windows of
// "topic: content", best kRecallRerankPassages by query-token overlap.
// Memory score = max passage logit.
std::vector<std::string> rerankPassages(const std::string& query, const MemoryNode& n);

// Pure: reorder the first scores.size() nodes by score desc (stable);
// mismatched / empty scores -> input returned unchanged.
std::vector<MemoryNode> reorderHeadByScore(std::vector<MemoryNode> nodes,
                                           const std::vector<float>& scores, int n);

// Candidates to fetch before reranking: max(limit, n) when n > 0.
int rerankFetchLimit(int limit, int n);

// Score the first n candidates with the shared cross-encoder pool, reorder,
// then cut to limit. *applied = false when the model is unavailable.
std::vector<MemoryNode> applyRecallRerank(const std::string& query,
                                          std::vector<MemoryNode> cand,
                                          int n, int limit, bool* applied = nullptr);

}  // namespace icmg::imem
