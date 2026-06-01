// v1.71.0 Graphify: pure graph analytics — confidence, centrality, god-nodes,
// report, viz HTML.

#include "../test_main.hpp"
#include "../../src/graph/graph_report.hpp"

#include <map>
#include <string>
#include <vector>

using namespace icmg::graph;

static GraphNode node(int64_t id, const std::string& path) {
    GraphNode n; n.id = id; n.path = path; n.zone = "default"; return n;
}
static GraphEdge edge(int64_t s, int64_t d, const std::string& t, double w = 1.0) {
    GraphEdge e; e.src = s; e.dst = d; e.edge_type = t; e.weight = w; return e;
}

TEST("graph-report: edge confidence by type + resolution") {
    ASSERT_TRUE(edgeConfidence(edge(1, -1, "imports")) < 0.4);   // unresolved low
    ASSERT_TRUE(edgeConfidence(edge(1, 2, "inherits")) > 0.95);  // structural high
    ASSERT_TRUE(edgeConfidence(edge(1, 2, "calls")) < edgeConfidence(edge(1, 2, "imports")));
}

TEST("graph-report: degree centrality counts in+out") {
    std::vector<GraphNode> ns = {node(1,"a"), node(2,"b"), node(3,"c")};
    // 1->2, 1->3, 2->3  : deg(1)=2 out, deg(2)=1in+1out=2, deg(3)=2in
    std::vector<GraphEdge> es = {edge(1,2,"imports"), edge(1,3,"imports"), edge(2,3,"calls")};
    auto deg = degreeCentrality(ns, es);
    ASSERT_EQ(deg[1], 2);
    ASSERT_EQ(deg[2], 2);
    ASSERT_EQ(deg[3], 2);
}

TEST("graph-report: unresolved edge does not credit dst in-degree") {
    std::vector<GraphNode> ns = {node(1,"a")};
    std::vector<GraphEdge> es = {edge(1,-1,"imports")};
    auto deg = degreeCentrality(ns, es);
    ASSERT_EQ(deg[1], 1);   // only src out-degree
}

TEST("graph-report: god-node detects high-degree hub") {
    std::vector<GraphNode> ns;
    for (int i = 1; i <= 10; ++i) ns.push_back(node(i, "n" + std::to_string(i)));
    std::vector<GraphEdge> es;
    // node 1 is a hub: every other node points to it
    for (int i = 2; i <= 10; ++i) es.push_back(edge(i, 1, "imports"));
    auto deg = degreeCentrality(ns, es);
    auto gods = godNodes(deg);
    ASSERT_TRUE(gods.size() >= 1);
    ASSERT_EQ(gods[0].first, (int64_t)1);   // hub is the top god-node
}

TEST("graph-report: empty graph -> no god-nodes, valid report") {
    auto gods = godNodes({});
    ASSERT_EQ(gods.size(), (size_t)0);
    std::string md = buildReportMd({}, {}, gods);
    ASSERT_CONTAINS(md, "Nodes: 0");
}

TEST("graph-report: markdown lists god-node paths") {
    std::vector<GraphNode> ns;
    for (int i = 1; i <= 6; ++i) ns.push_back(node(i, "f" + std::to_string(i) + ".cpp"));
    std::vector<GraphEdge> es;
    for (int i = 2; i <= 6; ++i) es.push_back(edge(i, 1, "imports"));
    auto deg = degreeCentrality(ns, es);
    std::string md = buildReportMd(ns, es, godNodes(deg));
    ASSERT_CONTAINS(md, "f1.cpp");
}

TEST("graph-viz: neutralizes </script> in node path (no script breakout)") {
    std::vector<GraphNode> ns = {node(1, "a</script><img src=x onerror=alert(1)>.cpp")};
    auto deg = degreeCentrality(ns, {});
    std::string html = buildVizHtml(ns, {}, deg);
    ASSERT_NOT_CONTAINS(html, "</script><img");   // raw breakout must not survive
    ASSERT_CONTAINS(html, "\\u003c");              // escaped form present
}

TEST("graph-viz: emits self-contained HTML with embedded data") {
    std::vector<GraphNode> ns = {node(1,"a.cpp"), node(2,"b.cpp")};
    std::vector<GraphEdge> es = {edge(1,2,"imports")};
    auto deg = degreeCentrality(ns, es);
    std::string html = buildVizHtml(ns, es, deg);
    ASSERT_CONTAINS(html, "<!DOCTYPE html>");
    ASSERT_CONTAINS(html, "d3.v7.min.js");
    ASSERT_CONTAINS(html, "a.cpp");
    ASSERT_CONTAINS(html, "forceSimulation");
}
