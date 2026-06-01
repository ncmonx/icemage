#pragma once
// v1.79.0 ICM dual-memory: AtomStore — enqueue + drain + atom-FTS recall.
// Atomization is OFF the hot path: store() enqueues a node id; the worker
// (drainQueue) splits content into atoms (heuristic / opt-in LLM) and inserts.
#include "../core/db.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::imem {

class AtomStore {
public:
    explicit AtomStore(core::Db& db) : db_(db) {}

    // Hot-path safe: one INSERT OR REPLACE into the queue (no split here).
    void enqueue(int64_t node_id, int64_t now_sec);

    // Worker path: pop up to `max` queued nodes, split each source node's
    // content (heuristic default, opt-in LLM via ICMG_ATOMIZE_LLM=1), dedup
    // within source, insert atoms. Returns number of nodes processed.
    int drainQueue(int max);

    // BM25 recall over atom FTS → deduped source_node_ids in rank order.
    std::vector<int64_t> recallAtomSources(const std::string& query, int limit);

private:
    core::Db& db_;
};

} // namespace icmg::imem
