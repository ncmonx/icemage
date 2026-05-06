#pragma once
#include "graph_node.hpp"
#include "../core/db.hpp"
#include <optional>
#include <vector>
#include <string>

namespace icmg::graph {

class GraphStore {
public:
    explicit GraphStore(core::Db& db);

    // Node CRUD
    int64_t upsertNode(const GraphNode& node);
    std::optional<GraphNode> getNode(const std::string& path);
    void removeNode(const std::string& path);  // cascades edges
    std::vector<GraphNode> all() const;
    bool isStale(const std::string& path, const std::string& hash) const;

    // Edge CRUD
    void upsertEdge(const GraphEdge& edge);
    std::vector<GraphEdge> edgesFrom(int64_t nodeId);
    std::vector<GraphEdge> edgesTo(int64_t nodeId);

    // Search / traversal
    std::vector<GraphNode> related(const std::string& path, int limit = 10);
    std::vector<GraphNode> search(const std::string& query, int limit = 10);

    // Impact analysis (A5): files that depend on `path`
    std::vector<GraphNode> impact(const std::string& path, int depth = 3);

    // Orphan detection (A4): nodes with no inbound edges
    std::vector<GraphNode> orphans(const std::vector<std::string>& exclude_patterns = {}) const;

    // Cycle detection (A3): DFS with color marking
    // Returns vector of cycles (each cycle = ordered list of paths)
    std::vector<std::vector<std::string>> cycles(const std::string& lang_filter = "") const;

    // SCC via Tarjan (A6)
    std::vector<std::vector<std::string>> scc() const;

    // Edge resolution pass (A7): 2-pass — resolve collected imports to node IDs
    // import_list: vector of (src_node_id, src_path, import_name_string)
    void resolveAndInsertEdges(
        const std::vector<std::tuple<int64_t,std::string,std::string>>& import_list);
    // Strategy 4: class cross-reference (Graphify-style) — always run after scan
    void buildXRefEdges();
    // Legacy incremental resolver (still used when calling graph-update on partial data)
    void resolveEdges();

    // Diff / scan history (A8)
    int64_t recordScanRun(const std::string& root, int node_count, int edge_count);
    // Hot files (A10)
    std::vector<GraphNode> hot(int days = 7, int limit = 20) const;
    void bumpAccess(int64_t nodeId);

    // Stats
    int nodeCount() const;
    int edgeCount() const;

private:
    core::Db& db_;
    GraphNode rowToNode(const core::Row& row) const;
    GraphEdge rowToEdge(const core::Row& row) const;
};

} // namespace icmg::graph
