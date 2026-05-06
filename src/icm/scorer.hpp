#pragma once
#include "memory_node.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace icmg::icm {

class Scorer {
public:
    // Singleton — shared across CLI commands in one invocation.
    static Scorer& instance();

    // Build IDF table from corpus. Must call before score/rank.
    void fit(const std::vector<MemoryNode>& corpus);

    // Mark corpus as stale (call on POST_STORE).
    void invalidate() { dirty_ = true; }
    bool isDirty()    const { return dirty_; }

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
    double recencyDecay(int64_t last_used) const;
    double idf(const std::string& term) const;
};

} // namespace icmg::icm
