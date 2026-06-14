#pragma once
// graph_sync_filter.hpp — pure predicate for incremental graph→memory sync.
//
// Bottleneck fixed (2026-06-14): `icmg graph update` (plain) ran
// syncGraphToMemory, which walked EVERY graph node and issued one BM25
// recallByTopic query PER FILE node (~7000 FTS queries) even when scan only
// touched ONE file. That made each update take minutes ("memory+7017").
//
// The scanner already knows exactly which files it updated. This pure helper
// decides whether a given node should be (re)synced: file-kind only, and — when
// an incremental changed-set is supplied — only paths in that set. An empty
// changed-set means "full sync" (preserves the old behavior for `graph scan`).
#include <set>
#include <string>

namespace icmg::graph {

inline bool shouldSyncNode(const std::string& path,
                           bool is_file_kind,
                           const std::set<std::string>& changed,
                           bool incremental) {
    if (!is_file_kind) return false;          // skip child symbol nodes
    if (!incremental)  return true;           // full sync: every file node
    return changed.count(path) > 0;           // incremental: only changed files
}

} // namespace icmg::graph
