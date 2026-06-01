#pragma once
// Phase 23: persists embeddings table writes/reads.
#include "embedder.hpp"
#include "../core/db.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::embed {

struct EmbedRow {
    int64_t              node_id = 0;
    std::string          kind;        // "memory" | "graph"
    std::vector<float>   vec;
    int                  dim = 0;
    std::string          model;
    std::string          body_hash;
};

class EmbedStore {
public:
    explicit EmbedStore(core::Db& db) : db_(db) {}

    // Upsert (overwrite on PK conflict).
    void put(int64_t node_id, const std::string& kind,
             const std::vector<float>& vec, const std::string& model,
             const std::string& body_hash);

    // Returns empty vec if missing.
    std::vector<float> get(int64_t node_id, const std::string& kind, int dim);

    // True if a row exists for this body_hash (skip re-embed).
    bool fresh(int64_t node_id, const std::string& kind, const std::string& body_hash);

    // Bulk fetch: returns map node_id -> vec. Used by hybrid recall reranker.
    std::vector<std::pair<int64_t, std::vector<float>>>
    getMany(const std::string& kind, const std::vector<int64_t>& ids, int dim);

    // Total embedded count.
    int count(const std::string& kind);

private:
    core::Db& db_;
};

} // namespace icmg::embed
