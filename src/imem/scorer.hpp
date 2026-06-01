#pragma once
#include "memory_node.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace icmg::imem {

class Scorer {
public:
    // Singleton — shared across CLI commands in one invocation.
    static Scorer& instance();

    // Build IDF table from corpus. Must call before score/rank.
    void fit(const std::vector<MemoryNode>& corpus);

    // Mark corpus as stale (call on POST_STORE).
    void invalidate() { dirty_ = true; }
    bool isDirty()    const { return dirty_; }

    // v1.29.0 mono-test groundwork: full state reset for tests sharing one
    // process. Clears corpus stats so each TEST() starts from a clean
    // Scorer instance without leaking BM25 weights from prior fit() calls.
    void reset() {
        df_.clear();
        N_ = 0;
        avgdl_ = 0.0;
        dirty_ = true;
    }

    // Full composite score for one node.
    double score(const std::string& query, const MemoryNode& node) const;

    // Breakdown for --explain output.
    struct ScoreDetail {
        double bm25;
        double recency;
        double freq;
        double importance_mult;
        double total;
        std::vector<std::string> matched_tokens;
    };
    ScoreDetail scoreDetailed(const std::string& query, const MemoryNode& node) const;

    // Rank corpus by score, return top limit nodes.
    std::vector<MemoryNode> rank(const std::string& query,
                                  std::vector<MemoryNode> nodes,
                                  int limit = 10) const;

    // v1.23.0: test-visible decay helpers (used by tests/imem/test_importance_decay.cpp).
    // Pure functions with no Scorer state — safe to expose.
    double recencyDecay(int64_t last_used) const;
    double accessAwareDecay(int64_t last_used, int freq) const;
    // v1.21.9 (M2): tier-aware decay — importance 3=critical (frozen),
    // 2=high (half rate), 1=medium (baseline), 0=low (double rate).
    double ageDecay(int64_t created_at, int importance = 1) const;

private:
    Scorer() = default;

    // BM25 parameters
    static constexpr double k1_    = 1.5;
    static constexpr double b_     = 0.75;

    std::unordered_map<std::string, int> df_;   // document frequency per term
    int    N_     = 0;     // corpus size
    double avgdl_ = 0.0;   // avg doc length (tokens)
    bool   dirty_ = true;

    std::vector<std::string> tokenize(const std::string& text) const;
    std::string document(const MemoryNode& node) const;
    double bm25(const std::string& query, const MemoryNode& node) const;
    // v1.20.0 (M1) accessAwareDecay + v1.21.9 (M2) ageDecay now public above.
    double idf(const std::string& term) const;
};

} // namespace icmg::imem
