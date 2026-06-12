#pragma once
// v2.0.0 repo skeleton: rank files by graph centrality, emit a budgeted
// signature outline. Pure + header-only (no DB) so it is unit-testable.
// PageRank upgrade (2026-06-12): score is a double (PageRank). Phase 3.1: drop
// vendored/third_party nodes AND (optionally) scope to the project root so the
// skeleton reflects YOUR code, not bundled headers or sibling projects that
// leaked into the graph via cross-project edges.
#include "graph_node.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace icmg::graph {

// Lowercase + forward-slash normalize (Windows paths compare case-insensitively).
inline std::string normPath(const std::string& path) {
    std::string p; p.reserve(path.size());
    for (char c : path) p += (c == '\\') ? '/' : (char)std::tolower((unsigned char)c);
    return p;
}

// True if `path` lives under a vendored / generated directory (segment match,
// not substring -- "src/vendored_notes.cpp" is NOT vendored, "a/vendor/b" is).
inline bool isVendoredPath(const std::string& path) {
    std::string s = "/" + normPath(path) + "/";   // pad so first/last segments match
    static const char* segs[] = {
        "/third_party/", "/node_modules/", "/vendor/", "/.git/", "/dist/",
        "/.venv/", "/site-packages/", "/external/", "/.cache/", "/target/",
    };
    for (const char* seg : segs) if (s.find(seg) != std::string::npos) return true;
    if (s.find("/build/")  != std::string::npos) return true;
    if (s.find("/build-")  != std::string::npos) return true;   // build-msvc-full, build-* dirs
    return false;
}

// True if `path` is inside `root` (both normalized; empty root => always true).
inline bool pathUnderRoot(const std::string& path, const std::string& root) {
    if (root.empty()) return true;
    std::string p = normPath(path), r = normPath(root);
    if (!r.empty() && r.back() == '/') r.pop_back();
    return p.size() >= r.size() && p.compare(0, r.size(), r) == 0;
}

// `score` = pageRank(nodes, edges) (see graph_centrality.hpp) -- or any
// id->importance map. Files are ranked by score desc; each emits its child
// symbol signatures, accumulating until the char budget is hit. The single top
// file is always included (never empty-out when there is input). `excludeVendored`
// (default) drops third_party/generated files; non-empty `rootPrefix` keeps only
// files inside that directory tree.
inline std::string buildRepoSkeleton(const std::vector<GraphNode>& nodes,
                                     const std::map<int64_t,double>& score,
                                     size_t budgetChars,
                                     bool excludeVendored = true,
                                     const std::string& rootPrefix = "") {
    std::vector<const GraphNode*> files;
    std::map<int64_t, std::vector<const GraphNode*>> kids;
    for (const auto& n : nodes) {
        if (n.kind == "file") {
            if (excludeVendored && isVendoredPath(n.path)) continue;
            if (!pathUnderRoot(n.path, rootPrefix)) continue;
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