#pragma once
// v2.0.0 externals (Temporal KG): time-aware graph ranking. Exponential decay on
// node age blended with centrality, so a recently-touched hub outranks a stale
// one. Pure + header-only (no DB); recency comes from node.updated_at. PageRank
// upgrade (2026-06-12): centrality is a double score (PageRank, always > 0 via
// the teleport floor) so it multiplies the recency weight directly. Shares the
// vendored/test/project-root hygiene filters with the repo skeleton.
#include "graph_node.hpp"
#include "path_filter.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace icmg::graph {

// base * 0.5^(age/halflife). halflife<=0 or age<=0 -> base (decay disabled / fresh).
inline double temporalWeight(double base, int64_t age_sec, int64_t halflife_sec) {
    if (halflife_sec <= 0 || age_sec <= 0) return base;
    return base * std::pow(0.5, (double)age_sec / (double)halflife_sec);
}

struct TemporalRank { int64_t id; double score; };

// Rank FILE nodes by centrality x recency-decay. `score` = pageRank(...) (see
// graph_centrality.hpp). Nodes with no updated_at are treated as very old
// (heavily decayed). A node absent from `score` contributes 0 (sorts last).
// Hygiene filters (path_filter.hpp): vendored/test files dropped by default,
// non-empty `rootPrefix` scopes to that tree.
inline std::vector<TemporalRank> rankByTemporal(const std::vector<GraphNode>& nodes,
                                                const std::map<int64_t,double>& score,
                                                int64_t now, int64_t halflife_sec,
                                                bool excludeVendored = true,
                                                const std::string& rootPrefix = "",
                                                bool includeTests = false) {
    std::vector<TemporalRank> out;
    for (const auto& n : nodes) {
        if (n.kind != "file") continue;
        if (!keepProjectFile(n.path, excludeVendored, includeTests, rootPrefix)) continue;
        auto it = score.find(n.id);
        double c = it == score.end() ? 0.0 : it->second;
        int64_t age = n.updated_at > 0 ? (now - n.updated_at)
                                       : (halflife_sec > 0 ? halflife_sec * 100 : 0);
        out.push_back({ n.id, c * temporalWeight(1.0, age, halflife_sec) });
    }
    std::sort(out.begin(), out.end(),
              [](const TemporalRank& a, const TemporalRank& b) { return a.score > b.score; });
    return out;
}

}  // namespace icmg::graph