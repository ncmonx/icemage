// v1.55 Sub-D D7: Leiden community detection algorithm.
//
// Reference: Traag, Waltman, van Eck (2019) — "From Louvain to Leiden:
// guaranteeing well-connected communities". Sci Rep 9, 5233.
// https://www.nature.com/articles/s41598-019-41695-z
//
// Three phases per pass:
//   1. Local moving of nodes (greedy modularity maximisation).
//   2. Refinement of the partition (per-community sub-clustering).
//   3. Aggregation of the network into a quotient graph; recurse.
//
// Returns a flat vector<int> cluster assignment indexed by input node id
// (node id = position in the input nodes vector).
//
// API is intentionally GraphStore-agnostic: caller assembles {nodes, edges}
// then invokes leidenCluster(...). A thin adapter pulls from GraphStore
// (see graph_cluster_cmd.cpp).

#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace icmg::graph {

struct LeidenEdge {
    int    u = 0;      // 0-indexed node id
    int    v = 0;      // 0-indexed node id
    double w = 1.0;    // weight (>0). Symmetric (undirected) graph assumed.
};

struct LeidenOptions {
    double resolution = 1.0;     // gamma — higher = more, smaller communities
    int    max_iter   = 10;      // upper bound on outer passes
    double tol_dq     = 1e-7;    // stop when ΔQ between passes drops below
    uint64_t seed     = 42ULL;   // RNG seed for tie-break ordering
    bool   refine     = true;    // false = pure Louvain (faster, no guarantee)
};

struct LeidenResult {
    std::vector<int> cluster;     // size = N; cluster[i] = community id of node i
    int    num_clusters = 0;      // count of distinct community ids
    double modularity   = 0.0;    // final Q (resolution-adjusted)
    int    passes       = 0;      // outer passes executed
};

// Cluster N nodes given an undirected edge list.
// Self-loops are allowed and contribute to internal community weight.
// Edges with u==v or u/v out-of-range are silently skipped.
LeidenResult leidenCluster(int num_nodes,
                           const std::vector<LeidenEdge>& edges,
                           const LeidenOptions& opts = {});

} // namespace icmg::graph
