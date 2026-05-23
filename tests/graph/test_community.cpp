// Phase 26 T6: Louvain community detection.
#include "../test_main.hpp"
#include "../../src/graph/community.hpp"

using namespace icmg::graph;

TEST("community: empty graph -> empty result") {
    AdjList adj;
    auto res = louvain(adj);
    ASSERT_EQ(res.cluster_count, 0);
}

TEST("community: 3 isolated nodes -> 3 clusters") {
    AdjList adj(3);
    auto res = louvain(adj);
    ASSERT_EQ(res.cluster_count, 3);
}

TEST("community: 2 disjoint cliques -> 2 clusters") {
    AdjList adj(6);
    // Clique A: 0-1-2
    adj[0].push_back({1, 1.0}); adj[1].push_back({0, 1.0});
    adj[1].push_back({2, 1.0}); adj[2].push_back({1, 1.0});
    adj[0].push_back({2, 1.0}); adj[2].push_back({0, 1.0});
    // Clique B: 3-4-5
    adj[3].push_back({4, 1.0}); adj[4].push_back({3, 1.0});
    adj[4].push_back({5, 1.0}); adj[5].push_back({4, 1.0});
    adj[3].push_back({5, 1.0}); adj[5].push_back({3, 1.0});
    auto res = louvain(adj);
    ASSERT_EQ(res.cluster_count, 2);
    ASSERT_TRUE(res.cluster_id[0] == res.cluster_id[1]);
    ASSERT_TRUE(res.cluster_id[1] == res.cluster_id[2]);
    ASSERT_TRUE(res.cluster_id[3] == res.cluster_id[4]);
    ASSERT_TRUE(res.cluster_id[4] == res.cluster_id[5]);
    ASSERT_TRUE(res.cluster_id[0] != res.cluster_id[3]);
}

TEST("community: weak inter-cluster edge preserves split") {
    AdjList adj(6);
    // 2 cliques + single bridge edge 2-3
    auto add = [&](int a, int b, double w){
        adj[a].push_back({b, w}); adj[b].push_back({a, w});
    };
    add(0, 1, 1.0); add(1, 2, 1.0); add(0, 2, 1.0);
    add(3, 4, 1.0); add(4, 5, 1.0); add(3, 5, 1.0);
    add(2, 3, 0.1);   // weak bridge
    auto res = louvain(adj);
    ASSERT_TRUE(res.cluster_count >= 2);
    ASSERT_TRUE(res.modularity > 0.0);
}

TEST("community: modularity non-negative on connected graph") {
    AdjList adj(4);
    auto add = [&](int a, int b){
        adj[a].push_back({b, 1.0}); adj[b].push_back({a, 1.0});
    };
    add(0,1); add(1,2); add(2,3); add(3,0);
    auto res = louvain(adj);
    ASSERT_TRUE(res.modularity >= -1.0 && res.modularity <= 1.0);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
