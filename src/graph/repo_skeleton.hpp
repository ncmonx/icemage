#pragma once
// v2.0.0 repo skeleton: rank files by graph centrality, emit a budgeted
// signature outline. Pure + header-only (no DB) so it is unit-testable.
// PageRank upgrade (2026-06-12): score is a double (PageRank). Hygiene filters
// (vendored / tests / project-root) live in path_filter.hpp and are shared with
// the temporal view so every codebase view stays consistent.
#include "graph_node.hpp"
#include "path_filter.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace icmg::graph {

// 2026-09-23 IDE-F (RepoAtlas, arXiv 2609.16936): focused task neighborhood.
// Under global PageRank a sparse task seed drowns in hub files; a focused
// view keeps only the seed nodes + their 1-hop neighbors (undirected -- a
// dependency is context in both directions). Empty seed -> empty set, caller
// falls back to the global view.
inline std::set<int64_t> focusNeighborhood(const std::vector<GraphEdge>& edges,
                                           const std::map<int64_t,double>& seed) {
    std::set<int64_t> allowed;
    if (seed.empty()) return allowed;
    for (const auto& [id, w] : seed) allowed.insert(id);
    for (const auto& e : edges) {
        if (seed.count(e.src)) allowed.insert(e.dst);
        if (seed.count(e.dst)) allowed.insert(e.src);
    }
    return allowed;
}

// `score` = pageRank(nodes, edges) (see graph_centrality.hpp) -- or any
// id->importance map. Files are ranked by score desc; each emits its child
// symbol signatures, accumulating until the char budget is hit. The single top
// file is always included (never empty-out when there is input). Filters
// (applied before ranking, see path_filter.hpp): `excludeVendored` (default)
// drops third_party/generated files; non-empty `rootPrefix` keeps only files
// inside that tree; when `includeTests` is false (default) test/spec files drop.
// `allowlist` (optional, IDE-F): only emit files whose id is in the set.
inline std::string buildRepoSkeleton(const std::vector<GraphNode>& nodes,
                                     const std::map<int64_t,double>& score,
                                     size_t budgetChars,
                                     bool excludeVendored = true,
                                     const std::string& rootPrefix = "",
                                     bool includeTests = false,
                                     const std::set<int64_t>* allowlist = nullptr) {
    std::vector<const GraphNode*> files;
    std::map<int64_t, std::vector<const GraphNode*>> kids;
    for (const auto& n : nodes) {
        if (n.kind == "file") {
            if (!keepProjectFile(n.path, excludeVendored, includeTests, rootPrefix)) continue;
            if (allowlist && allowlist->count(n.id) == 0) continue;
            files.push_back(&n);
        } else if (n.parent_id) {
            kids[n.parent_id].push_back(&n);
        }
    }
    if (files.empty()) return "";

    auto scoreOf = [&](int64_t id) {
        auto it = score.find(id);
        return it == score.end() ? 0.0 : it->second;
    };
    std::sort(files.begin(), files.end(), [&](const GraphNode* a, const GraphNode* b) {
        double da = scoreOf(a->id), db = scoreOf(b->id);
        if (da != db) return da > db;          // higher centrality first
        return a->path < b->path;              // stable tie-break
    });

    std::string out;
    bool first = true;
    for (const auto* f : files) {
        long long pr = std::llround(scoreOf(f->id) * 10000.0);   // PageRank scaled for readability
        std::string block = f->path + " [pr=" + std::to_string(pr) + "]\n";
        auto kit = kids.find(f->id);
        if (kit != kids.end()) {
            for (const auto* k : kit->second) {
                block += "  ";
                block += k->signature.empty() ? k->symbol_name : k->signature;
                block += "\n";
            }
        }
        if (!first && out.size() + block.size() > budgetChars) break;  // top always in
        out += block;
        first = false;
    }
    return out;
}

}  // namespace icmg::graph