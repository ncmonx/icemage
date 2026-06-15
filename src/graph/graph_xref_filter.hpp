#pragma once
// graph_xref_filter.hpp — pure predicates for incremental cross-reference
// (xref) edge rebuild.
//
// Bottleneck fixed (2026-06-14, follow-up to graph_sync_filter): plain
// `icmg graph update` unconditionally called GraphStore::buildXRefEdges(),
// which reads the FULL content of EVERY graph node (~7000 files) and, per file,
// scans the text for every declared class name -> O(nodes * classes * filelen)
// plus ~7000 disk reads on EVERY run. It ran even when scan changed nothing
// (observed: ~150s at "memory+0"), because xref ran regardless of `updated`.
//
// The scanner already knows which files it touched. These pure helpers let the
// incremental update path:
//   1. xrefShouldRun  — skip the whole xref pass when nothing changed.
//   2. xrefIsSourceNode — only read+scan the files the scan actually changed
//      (their outgoing "uses" edges are the only ones whose source content
//      could have changed). Unchanged-file cross-refs to newly declared classes
//      settle on the next full `graph scan` (xref is additive/upsert-only, so a
//      momentarily missing edge is low-harm). Full scan keeps whole-graph xref.
#include <set>
#include <string>
#include <cstddef>

namespace icmg::graph {

// Whether the xref pass should run at all.
//  - non-incremental (full `graph scan`): always run.
//  - incremental (`graph update`): run only if the scan changed >=1 file.
inline bool xrefShouldRun(std::size_t changed_count, bool incremental) {
    if (!incremental) return true;
    return changed_count > 0;
}

// Whether a given node's file should be read + scanned as an xref SOURCE.
//  - non-incremental: every node (preserves old behavior).
//  - incremental: only files the scan actually changed.
inline bool xrefIsSourceNode(const std::string& path,
                             const std::set<std::string>& changed,
                             bool incremental) {
    if (!incremental) return true;
    return changed.count(path) > 0;
}

} // namespace icmg::graph
