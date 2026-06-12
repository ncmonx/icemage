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
#include <string>
#include <vector>

namespace icmg::graph {

// `score` = pageRank(nodes, edges) (see graph_centrality.hpp) -- or any
// id->importance map. Files are ranked by score desc; each emits its child
// symbol signatures, accumulating until the char budget is hit. The single top
// file is always included (never empty-out when there is input). Filters
// (applied before ranking, see path_filter.hpp): `excludeVendored` (default)
// drops third_party/generated files; non-empty `rootPrefix` keeps only files
// inside that tree; when `includeTests` is false (default) test/spec files drop.
inline std::string buildRepoSkeleton(const std::vector<GraphNode>& nodes,
                                     const std::map<int64_t,double>& score,
                                     size_t budgetChars,
                                     bool excludeVendored = true,
                                     const std::string& rootPrefix = "",
                                     bool includeTests = false) {
    std::vector<const GraphNode*> files;
    std::map<int64_t, std::vector<const GraphNode*>> kids;
    for (const auto& n : nodes) {
        if (n.kind == "file") {
            if (!keepProjectFile(n.path, excludeVendored, includeTests, rootPrefix)) continue;
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