// v1.55 Sub-D D7: unit tests for Leiden community detection.
//
// Goal: lock the contract (well-separated cliques resolve to distinct
// clusters; resolution-adjusted Q is positive). Not a paper-grade
// validation suite — that requires reference graphs (Karate, LFR).

#include "../../src/graph/leiden.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

using icmg::graph::LeidenEdge;
using icmg::graph::LeidenOptions;
using icmg::graph::leidenCluster;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++g_fail; } \
} while (0)

// Build a graph of K disjoint cliques of size n. Within each clique every
// pair is connected with weight 1. Between cliques: zero edges.
static std::vector<LeidenEdge> kCliques(int K, int n) {
    std::vector<LeidenEdge> edges;
    for (int c = 0; c < K; ++c) {
        int base = c * n;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                edges.push_back({base + i, base + j, 1.0});
            }
        }
    }
    return edges;
}

static void testEmpty() {
    auto r = leidenCluster(0, {});
    CHECK(r.num_clusters == 0, "empty graph -> 0 clusters");
    CHECK(r.cluster.empty(), "empty graph -> empty assignment");
}

static void testSingleton() {
    auto r = leidenCluster(1, {});
    CHECK(r.num_clusters == 1, "one node -> 1 cluster");
    CHECK(r.cluster.size() == 1 && r.cluster[0] == 0, "single node assignment");
}

static void testTwoDisjointCliques() {
    // 2 cliques of size 5 with no bridge edges.
    auto edges = kCliques(2, 5);
    auto r = leidenCluster(10, edges);
    CHECK(r.num_clusters == 2, "2 disjoint cliques -> 2 clusters");
    // All nodes 0..4 same cluster; all 5..9 same; clusters differ.
    int c_left  = r.cluster[0];
    int c_right = r.cluster[5];
    CHECK(c_left != c_right, "left vs right clique distinct cluster id");
    for (int i = 0; i < 5; ++i) CHECK(r.cluster[i] == c_left,  "left clique cohesive");
    for (int i = 5; i < 10; ++i) CHECK(r.cluster[i] == c_right, "right clique cohesive");
    CHECK(r.modularity > 0.3, "modularity high on well-separated cliques");
}

static void testThreeCliquesWithWeakBridges() {
    // 3 cliques of size 4 with single bridge edges (low weight) between
    // adjacent cliques. Leiden should still find 3 clusters.
    auto edges = kCliques(3, 4);
    edges.push_back({0,  4,  0.1});   // bridge clique 0 <-> 1
    edges.push_back({4,  8,  0.1});   // bridge clique 1 <-> 2
    auto r = leidenCluster(12, edges);
    CHECK(r.num_clusters == 3, "3 cliques with weak bridges -> 3 clusters");
    std::set<int> labels(r.cluster.begin(), r.cluster.end());
    CHECK((int)labels.size() == 3, "3 distinct labels emitted");
}

static void testLouvainModeNoRefine() {
    auto edges = kCliques(2, 5);
    LeidenOptions opts;
    opts.refine = false;  // pure Louvain
    auto r = leidenCluster(10, edges, opts);
    CHECK(r.num_clusters == 2, "Louvain mode also yields 2 on disjoint cliques");
}

static void testIdsCompacted() {
    // Even with internal aggregation churn, output cluster ids must be in
    // [0, num_clusters).
    auto edges = kCliques(4, 3);
    auto r = leidenCluster(12, edges);
    for (int c : r.cluster) {
        CHECK(c >= 0 && c < r.num_clusters, "cluster id in [0, K)");
    }
}

int main() {
    testEmpty();
    testSingleton();
    testTwoDisjointCliques();
    testThreeCliquesWithWeakBridges();
    testLouvainModeNoRefine();
    testIdsCompacted();
    if (g_fail == 0) {
        std::printf("test_leiden: all PASS\n");
        return 0;
    }
    std::printf("test_leiden: %d FAIL\n", g_fail);
    return 1;
}
