#pragma once
#include "memory_node.hpp"
#include "../core/db.hpp"
#include <string>
#include <vector>
#include <stdexcept>

namespace icmg::icm {

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

    // Recall filtered by topic prefix.
    std::vector<MemoryNode> recallByTopic(const std::string& topic, int limit = 10);

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

    MemoryNode rowToNode(const core::Row& row) const;
    std::vector<MemoryNode> findSimilar(const std::string& topic,
                                         const std::string& content,
                                         double threshold = 0.85) const;
    double jaccardSimilarity(const std::string& a, const std::string& b) const;
    void   syncKeywords(int64_t id, const std::string& keywords);
    int64_t nowEpoch() const;
};

} // namespace icmg::icm
