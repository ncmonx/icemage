// PageRank / Personalized PageRank — pure-core unit tests (TDD-first, 2026-06-12).
//
// Upgrade over degreeCentrality (graph_report.hpp): degree counts only direct
// edges; PageRank propagates importance transitively and is confidence-weighted
// (edgeConfidence: inherits 0.97 > calls 0.70 > unresolved 0.30). Personalized
// PageRank biases the teleport vector toward task-seed nodes for task-aware
// ranking in `icmg pack`. Pure (nodes+edges in, map<id,double> out) so it is
// unit-testable without a DB — same ethos as graph_report.hpp.

#include "../test_main.hpp"
#include "../../src/graph/graph_centrality.hpp"
#include <cmath>

using namespace icmg::graph;

namespace {
GraphNode fileNode(int64_t id) { GraphNode n; n.id = id; n.kind = "file"; n.path = "f" + std::to_string(id); return n; }
GraphEdge edge(int64_t s, int64_t d, const std::string& t = "imports", double w = 1.0) {
    GraphEdge e; e.src = s; e.dst = d; e.edge_type = t; e.weight = w; return e;
}
double total(const std::map<int64_t,double>& m) { double s = 0; for (auto& kv : m) s += kv.second; return s; }
}  // namespace

TEST("pagerank: scores form a probability distribution (sum ~ 1)") {
    std::vector<GraphNode> nodes = { fileNode(1), fileNode(2), fileNode(3) };
    std::vector<GraphEdge> edges = { edge(1,2), edge(2,3), edge(3,1) };  // triangle
    auto pr = pageRank(nodes, edges);
    ASSERT_EQ(pr.size(), (size_t)3);
    ASSERT_TRUE(std::fabs(total(pr) - 1.0) < 1e-3);
    // symmetric triangle -> roughly equal mass
    ASSERT_TRUE(std::fabs(pr[1] - pr[2]) < 1e-2);
}

TEST("pagerank: a hub (many referrers) outranks a leaf") {
    std::vector<GraphNode> nodes = { fileNode(1), fileNode(2), fileNode(3), fileNode(4) };
    std::vector<GraphEdge> edges = { edge(2,1), edge(3,1), edge(4,1) };  // 1 referenced by 2,3,4
    auto pr = pageRank(nodes, edges);
    ASSERT_TRUE(pr[1] > pr[2]);
    ASSERT_TRUE(pr[1] > pr[3]);
    ASSERT_TRUE(pr[1] > pr[4]);
}

TEST("pagerank: importance is transitive (referenced-by-hub beats referenced-by-leaf)") {
    // 1 is a hub (in from 5,6,7). 2 is referenced only by the hub 1.
    // 3 is referenced by two leaves 8,9. 2 should outrank 3.
    std::vector<GraphNode> nodes;
    for (int64_t i = 1; i <= 9; ++i) nodes.push_back(fileNode(i));
    std::vector<GraphEdge> edges = {
        edge(5,1), edge(6,1), edge(7,1),  // 1 = hub
        edge(1,2),                        // 2 fed by the hub
        edge(8,3), edge(9,3),             // 3 fed by two leaves
    };
    auto pr = pageRank(nodes, edges);
    ASSERT_TRUE(pr[2] > pr[3]);
}

TEST("pagerank: confidence-weighted (inherits edge carries more mass than calls)") {
    // Same source 1 points to 2 via inherits (0.97) and to 3 via calls (0.70).
    std::vector<GraphNode> nodes = { fileNode(1), fileNode(2), fileNode(3) };
    std::vector<GraphEdge> edges = { edge(1,2,"inherits"), edge(1,3,"calls") };
    auto pr = pageRank(nodes, edges);
    ASSERT_TRUE(pr[2] > pr[3]);
}

TEST("pagerank: personalization biases mass toward the seed") {
    std::vector<GraphNode> nodes = { fileNode(1), fileNode(2), fileNode(3) };
    std::vector<GraphEdge> edges = { edge(1,2), edge(2,3), edge(3,1) };
    auto base = pageRank(nodes, edges);
    std::map<int64_t,double> seed = { {2, 1.0} };
    auto pers = personalizedPageRank(nodes, edges, seed);
    ASSERT_TRUE(std::fabs(total(pers) - 1.0) < 1e-3);
    ASSERT_TRUE(pers[2] > base[2]);   // seeded node gains mass vs uniform
}

TEST("pagerank: unresolved/dangling edges produce no NaN and still sum ~ 1") {
    std::vector<GraphNode> nodes = { fileNode(1), fileNode(2) };
    std::vector<GraphEdge> edges = {
        edge(1,-1,"imports"),   // unresolved target (dangling out-link)
        edge(2,1,"calls"),
    };
    auto pr = pageRank(nodes, edges);
    for (auto& kv : pr) ASSERT_TRUE(!std::isnan(kv.second) && !std::isinf(kv.second));
    ASSERT_TRUE(std::fabs(total(pr) - 1.0) < 1e-3);
}

TEST("pagerank: empty graph -> empty; single node -> all mass") {
    std::vector<GraphNode> none;
    std::vector<GraphEdge> noedge;
    ASSERT_TRUE(pageRank(none, noedge).empty());
    std::vector<GraphNode> one = { fileNode(7) };
    auto pr = pageRank(one, noedge);
    ASSERT_EQ(pr.size(), (size_t)1);
    ASSERT_TRUE(std::fabs(pr[7] - 1.0) < 1e-3);
}

TEST("pagerank: deterministic for fixed iterations") {
    std::vector<GraphNode> nodes = { fileNode(1), fileNode(2), fileNode(3), fileNode(4) };
    std::vector<GraphEdge> edges = { edge(1,2), edge(2,3), edge(3,4), edge(4,2), edge(1,3) };
    auto a = pageRank(nodes, edges);
    auto b = pageRank(nodes, edges);
    for (auto& kv : a) ASSERT_TRUE(std::fabs(kv.second - b[kv.first]) < 1e-12);
}

// ---- seedFromTask (personalization vector for task-aware ranking) ----------

TEST("seedFromTask: node whose symbol_name contains a task token is seeded") {
    std::vector<GraphNode> nodes;
    GraphNode a; a.id=1; a.kind="function"; a.symbol_name="pageRank"; a.path="src/graph/graph_centrality.hpp";
    GraphNode b; b.id=2; b.kind="function"; b.symbol_name="unrelated"; b.path="src/x.cpp";
    nodes = {a, b};
    auto seed = seedFromTask(nodes, "improve pagerank ranking");
    ASSERT_TRUE(seed.count(1) && seed[1] > 0);
    ASSERT_TRUE(seed.find(2) == seed.end());
}

TEST("seedFromTask: more token matches -> higher weight") {
    std::vector<GraphNode> nodes;
    GraphNode a; a.id=1; a.kind="file";     a.path="graph_centrality.hpp"; a.symbol_name="";
    GraphNode b; b.id=2; b.kind="function"; b.path="graph_centrality.hpp"; b.symbol_name="pageRank";
    nodes = {a, b};
    auto seed = seedFromTask(nodes, "graph centrality pagerank");
    ASSERT_TRUE(seed[2] > seed[1]);   // b also matches the 'pagerank' symbol token
}

TEST("seedFromTask: short tokens ignored; only-short task -> empty seed") {
    std::vector<GraphNode> nodes;
    GraphNode a; a.id=1; a.kind="function"; a.symbol_name="parser"; a.path="p.cpp";
    nodes = {a};
    auto s1 = seedFromTask(nodes, "fix the parser");      // 'parser' (>=3) matches
    ASSERT_TRUE(s1.count(1));
    auto s2 = seedFromTask(nodes, "a to in of");          // all < 3 chars -> no tokens
    ASSERT_TRUE(s2.empty());
}

TEST("seedFromTask: case-insensitive on symbol_name and path basename") {
    std::vector<GraphNode> nodes;
    GraphNode a; a.id=1; a.kind="function"; a.symbol_name="PageRank"; a.path="src/Graph/GraphStore.cpp";
    nodes = {a};
    auto seed = seedFromTask(nodes, "PAGERANK graphstore");
    ASSERT_TRUE(seed.count(1) && seed[1] >= 2);   // pagerank + graphstore both match
}
