#pragma once
#include "graph_node.hpp"
#include "node_bloom.hpp"   // v1.59 F3: negative-lookup gate
#include "../core/db.hpp"
#include <optional>
#include <unordered_map>
#include <vector>
#include <list>
#include <string>

namespace icmg::graph {

class GraphStore {
public:
    explicit GraphStore(core::Db& db);

    // Access underlying DB (for callers that need it for related tables, e.g. zone_config).
    core::Db& db() { return db_; }

    // Node CRUD
    int64_t upsertNode(const GraphNode& node);
    std::optional<GraphNode> getNode(const std::string& path);

    // v1.29.0 #10: enumerate ALL graph_nodes whose path basename matches.
    // Used by `icmg context` to warn on ambiguous lookups ("header.tsx"
    // matches 19 files; current getNode silently picks first). Empty
    // vector when no match; size 1 when unique; size >1 when ambiguous.
    std::vector<GraphNode> findByBasename(const std::string& basename);

    void removeNode(const std::string& path);  // cascades edges
    std::vector<GraphNode> all() const;
    bool isStale(const std::string& path, const std::string& hash) const;

    // Phase 18: symbol-aware queries
    std::vector<GraphNode> childrenOf(int64_t parent_id);
    std::vector<GraphNode> findSymbol(const std::string& name);  // by symbol_name
    void removeSymbolsOf(int64_t parent_id);  // delete all symbols under a file (for rescan)

    // Phase 21 hotfix: merge case-mismatched duplicate path nodes (Windows).
    // Returns count of duplicates merged. Idempotent — safe to call repeatedly.
    int dedupeCaseMixedPaths();

    // Phase 22: transitive impact (BFS forward closure with cycle detection).
    // edge_types empty → all types. reverse=true walks src←dst (who depends on me).
    std::vector<int64_t> closure(int64_t start,
                                  const std::vector<std::string>& edge_types,
                                  int max_depth,
                                  bool reverse = false);

    // BFS shortest path (forward edges src→dst). Returns ordered path from→to,
    // empty if unreachable or nodes not found. edge_types empty → all types.
    std::vector<std::string> shortestPath(const std::string& from,
                                           const std::string& to,
                                           const std::vector<std::string>& edge_types = {},
                                           int max_depth = 30);

    // BFS returning nodes grouped by distance level (level 0 = direct neighbors).
    // reverse=true walks inbound edges.
    std::vector<std::vector<GraphNode>> closureByLevel(int64_t start,
                                                        const std::vector<std::string>& edge_types,
                                                        int max_depth,
                                                        bool reverse = false);

    // BFS reverse from both nodes; returns intersection (shared upstream dependencies).
    std::vector<GraphNode> commonAncestors(const std::string& a,
                                            const std::string& b,
                                            int max_depth = 15);

    // Multi-source reverse BFS — union of impact from all paths.
    std::vector<GraphNode> impactAll(const std::vector<std::string>& paths, int depth = 3);

    // Edge CRUD
    void upsertEdge(const GraphEdge& edge);
    std::vector<GraphEdge> edgesFrom(int64_t nodeId);
    std::vector<GraphEdge> edgesTo(int64_t nodeId);

    // Search / traversal
    std::vector<GraphNode> related(const std::string& path, int limit = 10);
    std::vector<GraphNode> search(const std::string& query, int limit = 10);

    // Impact analysis (A5): files that depend on `path`. edge_types empty → all types.
    std::vector<GraphNode> impact(const std::string& path,
                                   int depth = 3,
                                   const std::vector<std::string>& edge_types = {});

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
    // Visual Studio designer file grouping: treats .cs + .Designer.cs + .resx
    // as one logical entity by assigning them the same group_id.
    // Also inserts "companion" edges between the trio.
    void groupDesignerTriples();
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

    // v1.21.8 (S1): clear in-memory caches. Called automatically on any
    // mutating method (upsertNode/removeNode/etc). Exposed publicly for
    // callers that mutate via direct SQL (e.g. scan_db helpers).
    void clearCache();

private:
    core::Db& db_;
    GraphNode rowToNode(const core::Row& row) const;
    GraphEdge rowToEdge(const core::Row& row) const;

    // v1.21.8 (S1): in-RAM cache for getNode(path). Bounded FIFO eviction.
    // Coarse invalidation: any write op clears the whole cache (simple +
    // safe — graph mutations are typically batched in scan passes, so we
    // pay invalidation cost once per scan, not per edit). Opt-out via
    // ICMG_NO_GRAPH_CACHE=1 (checked lazily).
    // v1.45.0 C2: upgraded FIFO -> LRU. node_cache_order_ now std::list
    // for O(1) splice-to-back on cache hit. iter map gives O(1) move.
    // Bumped cap 256 -> 512 (heavier RAM but bigger hit rate).
    mutable std::unordered_map<std::string, GraphNode> node_cache_;
    mutable std::list<std::string> node_cache_order_;
    mutable std::unordered_map<std::string, std::list<std::string>::iterator> node_cache_iters_;
    static constexpr size_t NODE_CACHE_MAX = 512;
    bool cacheEnabled() const;
    std::optional<GraphNode> cacheGetNode(const std::string& path) const;
    void cachePutNode(const std::string& path, const GraphNode& node) const;

    // v1.59 F3: bloom-filter negative-lookup gate for getNode. Seeded lazily
    // from EVERY node path + basename, so a getNode whose path-variants AND
    // basename are all bloom-absent can return nullopt without any SQL.
    // No false negatives: any node getNode could match has both its path and
    // basename in the filter. removeNode leaves stale bits (harmless — just
    // forces an SQL miss). Opt-out: ICMG_NO_GRAPH_BLOOM=1.
    mutable NodeBloom node_bloom_;
    mutable bool bloom_seeded_ = false;
    bool bloomEnabled() const;
    void ensureBloomSeeded() const;
    void bloomAddPath(const std::string& path) const;  // adds path + basename
};

} // namespace icmg::graph
