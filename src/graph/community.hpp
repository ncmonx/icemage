#pragma once
// Phase 26 T6: Louvain modularity-greedy community detection.
// Single-pass: each node tries to move to neighbor's community; converges when
// no Δmodularity > 0 move remains. Good enough for file-level graphs (<5K nodes).
//
// API:
//   detectCommunities(adjacency_list, weights?) -> vector<community_id>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace icmg::graph {

struct CommunityResult {
    std::vector<int>    cluster_id;     // index = node, value = cluster
    int                 cluster_count = 0;
    double              modularity = 0.0;
};

// Adjacency: vector<vector<{neighbor_idx, weight}>>
using AdjList = std::vector<std::vector<std::pair<int, double>>>;

CommunityResult louvain(const AdjList& adj, int max_iter = 20);

} // namespace icmg::graph
