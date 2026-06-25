// v1.55 Sub-D D7: unit tests for Leiden community detection.
//
// Goal: lock the contract (well-separated cliques resolve to distinct
// clusters; resolution-adjusted Q is positive). Not a paper-grade
// validation suite — that requires reference graphs (Karate, LFR).
//
// Ported to test_main.hpp harness (was standalone main) so it runs
// inside the mono icmg_test binary.

#include "../test_main.hpp"
#include "../../src/graph/leiden.hpp"

#include <set>
#include <vector>

using icmg::graph::LeidenEdge;
using icmg::graph::LeidenOptions;
using icmg::graph::leidenCluster;

// Build a graph of K disjoint cliques of size n.
static std::vector<LeidenEdge> kCliques(int K, int n) {
    std::vector<LeidenEdge> edges;
    for (int c = 0; c < K; ++c) {
        int base = c * n;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                edges.push_back({base + i, base + j, 1.0});
    }
    return edges;
}

TEST("leiden: empty graph") {
    auto r = leidenCluster(0, {});
    ASSERT_EQ(r.num_clusters, 0);
    ASSERT_TRUE(r.cluster.empty());
}

TEST("leiden: singleton") {
    auto r = leidenCluster(1, {});
    ASSERT_EQ(r.num_clusters, 1);
    ASSERT_TRUE(r.cluster.size() == 1 && r.cluster[0] == 0);
}

TEST("leiden: two disjoint cliques -> 2 clusters") {
    auto edges = kCliques(2, 5);
    auto r = leidenCluster(10, edges);
    ASSERT_EQ(r.num_clusters, 2);
    int c_left = r.cluster[0], c_right = r.cluster[5];
    ASSERT_TRUE(c_left != c_right);
    for (int i = 0;  i < 5;  ++i) ASSERT_TRUE(r.cluster[i] == c_left);
    for (int i = 5;  i < 10; ++i) ASSERT_TRUE(r.cluster[i] == c_right);
    ASSERT_TRUE(r.modularity > 0.3);
}

TEST("leiden: three cliques with weak bridges -> 3 clusters") {
    auto edges = kCliques(3, 4);
    edges.push_back({0,  4,  0.1});
    edges.push_back({4,  8,  0.1});
    auto r = leidenCluster(12, edges);
    ASSERT_EQ(r.num_clusters, 3);
    std::set<int> labels(r.cluster.begin(), r.cluster.end());
    ASSERT_TRUE((int)labels.size() == 3);
}

TEST("leiden: louvain mode (no refine)") {
    auto edges = kCliques(2, 5);
    LeidenOptions opts;
    opts.refine = false;
    auto r = leidenCluster(10, edges, opts);
    ASSERT_EQ(r.num_clusters, 2);
}

TEST("leiden: cluster ids compacted into [0, K)") {
    auto edges = kCliques(4, 3);
    auto r = leidenCluster(12, edges);
    for (int c : r.cluster)
        ASSERT_TRUE(c >= 0 && c < r.num_clusters);
}
