#pragma once
#include "memory_node.hpp"
#include "../core/db.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>

namespace icmg::imem {

class DuplicateError : public std::runtime_error {
public:
    explicit DuplicateError(const std::string& msg) : std::runtime_error(msg) {}
    int64_t existing_id = 0;
};

class MemoryStore {
public:
    explicit MemoryStore(core::Db& db);

    // Store new node. Throws DuplicateError if too similar (use force=true to bypass).
    int64_t store(const MemoryNode& node, bool force = false);

    // Update content of existing node.
    bool update(int64_t id, const std::string& content, const std::string& keywords = "");

    // Soft-delete (sets deleted_at). Returns false if not found.
    bool remove(int64_t id);

    // Restore soft-deleted node.
    bool restore(int64_t id);

    // Hard-delete all nodes deleted more than days_old days ago.
    int purge(int days_old = 30);

    // Ranked recall via BM25 + recency + importance. Skips deleted + expired.
    std::vector<MemoryNode> recall(const std::string& query,
                                   int limit    = 10,
                                   bool fuzzy   = false);

    // Zone-scoped recall: corpus restricted to nodes with matching zone before BM25 fit.
    // Sharper IDF + faster on large stores.
    std::vector<MemoryNode> recallInZone(const std::string& query,
                                          const std::string& zone,
                                          int limit  = 10,
                                          bool fuzzy = false);

    // Recall filtered by topic prefix.
    std::vector<MemoryNode> recallByTopic(const std::string& topic, int limit = 10);

    // Phase 23: hybrid BM25 + semantic recall.
    //   alpha=1.0 -> pure BM25 (default behavior)
    //   alpha=0.0 -> pure cosine (vec only)
    //   default 0.5 -> 50/50 blend
    // Falls back silently to BM25 if no embeddings available.
    std::vector<MemoryNode> recallSemantic(const std::string& query,
                                           int    limit = 10,
                                           double alpha = 0.5);

    // Bump frequency + update last_used.
    void bumpFrequency(int64_t id);

    // Get single node by id (including deleted).
    MemoryNode get(int64_t id) const;

    // All non-deleted nodes (for scorer corpus).
    std::vector<MemoryNode> all() const;

    // Save query to history.
    void logQuery(const std::string& query, int result_count);

    // Recent queries.
    std::vector<std::string> queryHistory(int limit = 20) const;

private:
    core::Db& db_;

    // v1.1.0 Task 3: in-memory embed cache.
    // Lazily populated by recallSemantic — avoids per-query DB roundtrip
    // for vec fetch. Cleared on store() to stay coherent with writes.
    mutable std::unordered_map<int64_t, std::vector<float>> embed_cache_;
    mutable bool embed_cache_warmed_ = false;
    void warmEmbedCache(int dim) const;

    MemoryNode rowToNode(const core::Row& row) const;
    std::vector<MemoryNode> findSimilar(const std::string& topic,
                                         const std::string& content,
                                         double threshold = 0.85) const;
    double jaccardSimilarity(const std::string& a, const std::string& b) const;
    void   syncKeywords(int64_t id, const std::string& keywords);
    int64_t nowEpoch() const;
};

} // namespace icmg::imem
