#pragma once
#include "context_node.hpp"
#include "db.hpp"
#include <optional>
#include <vector>
#include <string>

namespace icmg::core {

class ContextNodeStore {
public:
    explicit ContextNodeStore(Db& db);

    // Insert or update by node_key (idempotent).
    int64_t upsert(const ContextNode& node);

    // Lookup by node_key.
    std::optional<ContextNode> get(const std::string& node_key) const;

    // List nodes, optionally filtered by tier ("hot"/"cold"/"skill"/"" = all).
    std::vector<ContextNode> list(const std::string& tier = "",
                                  bool active_only = true) const;

    // BM25-inspired search over title+content+tags. Returns top `limit` matches.
    // tier_filter: "" = search all tiers.
    std::vector<ContextNode> search(const std::string& query,
                                    const std::string& tier_filter = "",
                                    int limit = 5,
                                    double min_score = 0.05) const;

    // Toggle active flag.
    bool setActive(const std::string& node_key, bool active);

    // Hard delete.
    bool remove(const std::string& node_key);

    // Count rows matching tier ("" = all).
    int count(const std::string& tier = "") const;

private:
    Db& db_;

    static ContextNode fromRow(const Row& r);

    // Tokenize string to lowercase words (≥2 chars).
    static std::vector<std::string> tokenize(const std::string& text);

    // Score query against a node (weighted TF over title×3 + tags×2 + content×1).
    static double scoreNode(const std::vector<std::string>& query_tokens,
                             const ContextNode& node);
};

} // namespace icmg::core
