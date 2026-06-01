#pragma once
// v2.0.0 Phase 0b: graph prune selector. Pure (no filesystem) so it is unit-
// testable — the caller injects an existence predicate. Used by
// `icmg graph prune --missing` to drop scan-pollution: dead temp / build /
// AppData nodes whose backing file no longer exists (known-issue #30469).
#include "graph_node.hpp"

#include <functional>
#include <string>
#include <vector>

namespace icmg::graph {

// Return paths of FILE nodes whose backing file is gone (per existsFn).
// Symbol-level nodes (kind != "file") are never returned — they have no 1:1
// disk file and are removed transitively when their parent file is pruned.
// Empty paths are skipped defensively.
inline std::vector<std::string> selectMissingNodes(
    const std::vector<GraphNode>& nodes,
    const std::function<bool(const std::string&)>& existsFn) {
    std::vector<std::string> gone;
    for (const auto& n : nodes) {
        if (n.kind != "file") continue;
        if (n.path.empty())    continue;
        if (!existsFn(n.path))  gone.push_back(n.path);
    }
    return gone;
}

}  // namespace icmg::graph
