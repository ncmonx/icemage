#pragma once
// v2.0.0 externals (Temporal KG): time-aware graph ranking. Exponential decay on
// node age blended with degree-centrality, so a recently-touched hub outranks a
// stale one. Pure + header-only (no DB); recency comes from node.updated_at.
#include "graph_node.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace icmg::graph {

// base * 0.5^(age/halflife). halflife<=0 or age<=0 → base (decay disabled / fresh).
inline double temporalWeight(double base, int64_t age_sec, int64_t halflife_sec) {
    if (halflife_sec <= 0 || age_sec <= 0) return base;
    return base * std::pow(0.5, (double)age_sec / (double)halflife_sec);
}

struct TemporalRank { int64_t id; double score; };

// Rank FILE nodes by (centrality+1) × recency-decay. `deg` = degreeCentrality.
// Nodes with no updated_at are treated as very old (heavily decayed).
inline std::vector<TemporalRank> rankByTemporal(const std::vector<GraphNode>& nodes,
                                                const std::map<int64_t,int>& deg,
                                                int64_t now, int64_t halflife_sec) {
    std::vector<TemporalRank> out;
    for (const auto& n : nodes) {
        if (n.kind != "file") continue;
        int d = deg.count(n.id) ? deg.at(n.id) : 0;
        int64_t age = n.updated_at > 0 ? (now - n.updated_at)
                                       : (halflife_sec > 0 ? halflife_sec * 100 : 0);
        out.push_back({ n.id, (double)(d + 1) * temporalWeight(1.0, age, halflife_sec) });
    }
    std::sort(out.begin(), out.end(),
              [](const TemporalRank& a, const TemporalRank& b) { return a.score > b.score; });
    return out;
}

}  // namespace icmg::graph
