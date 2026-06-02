#pragma once
// v2.0.0 externals (Ultra Compression / repo-compact): build a token-budgeted repo
// skeleton — most-connected files first, each followed by its symbol signatures.
// Pure + header-only (no DB) so it is unit-testable; centrality is injected.
#include "graph_node.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace icmg::graph {

// `deg` = degreeCentrality(nodes, edges) (see graph_report.hpp). Files are ranked
// by centrality desc; each emits its child symbol signatures, accumulating until
// the char budget is hit. The single top file is always included (never empty-out
// when there is input).
inline std::string buildRepoSkeleton(const std::vector<GraphNode>& nodes,
                                     const std::map<int64_t,int>& deg,
                                     size_t budgetChars) {
    std::vector<const GraphNode*> files;
    std::map<int64_t, std::vector<const GraphNode*>> kids;
    for (const auto& n : nodes) {
        if (n.kind == "file") files.push_back(&n);
        else if (n.parent_id) kids[n.parent_id].push_back(&n);
    }
    if (files.empty()) return "";

    auto degOf = [&](int64_t id) {
        auto it = deg.find(id);
        return it == deg.end() ? 0 : it->second;
    };
    std::sort(files.begin(), files.end(), [&](const GraphNode* a, const GraphNode* b) {
        int da = degOf(a->id), db = degOf(b->id);
        if (da != db) return da > db;          // higher centrality first
        return a->path < b->path;              // stable tie-break
    });

    std::string out;
    bool first = true;
    for (const auto* f : files) {
        std::string block = f->path + " [deg=" + std::to_string(degOf(f->id)) + "]\n";
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
