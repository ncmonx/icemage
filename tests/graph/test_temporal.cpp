// v2.0.0 externals (Temporal KG): time-aware graph ranking — recent nodes weigh
// more via exponential decay on age, blended with degree-centrality. Pure (no DB).

#include "../test_main.hpp"
#include "../../src/graph/temporal.hpp"
#include "../../src/graph/graph_node.hpp"

#include <map>
#include <vector>

using namespace icmg::graph;

namespace { const int64_t DAY = 86400; const int64_t NOW = 1'000'000'000; }

TEST("temporalWeight: age 0 = base; one half-life = half; two = quarter") {
    ASSERT_EQ((int)(temporalWeight(1.0, 0, 10*DAY)*1000), 1000);
    ASSERT_EQ((int)(temporalWeight(1.0, 10*DAY, 10*DAY)*1000), 500);
    ASSERT_EQ((int)(temporalWeight(1.0, 20*DAY, 10*DAY)*1000), 250);
}

TEST("temporalWeight: non-positive half-life = base (decay disabled)") {
    ASSERT_EQ((int)(temporalWeight(2.0, 99*DAY, 0)*1000), 2000);
}

TEST("rankByTemporal: recent file outranks older one of equal centrality") {
    std::vector<GraphNode> nodes;
    GraphNode a; a.id=1; a.kind="file"; a.path="recent.cpp"; a.updated_at=NOW-1*DAY;
    GraphNode b; b.id=2; b.kind="file"; b.path="old.cpp";    b.updated_at=NOW-100*DAY;
    nodes={a,b};
    std::map<int64_t,int> deg{{1,5},{2,5}};
    auto r = rankByTemporal(nodes, deg, NOW, 14*DAY);
    ASSERT_EQ((int)r.size(), 2);
    ASSERT_EQ((int)r[0].id, 1);   // recent first
}

TEST("rankByTemporal: skips symbol nodes") {
    std::vector<GraphNode> nodes;
    GraphNode s; s.id=1; s.kind="function"; s.path="x.cpp"; s.symbol_name="f"; s.updated_at=NOW;
    nodes={s};
    auto r = rankByTemporal(nodes, {}, NOW, 14*DAY);
    ASSERT_EQ((int)r.size(), 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
