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

// --- xref edge SCOPE (2026-11-28) ------------------------------------------
// Word-presence xref linked e.g. src/cli/find_slices.hpp to 70 third_party /
// out-of-project files (onnxruntime, llama.cpp, a sibling repo) because a class
// name declared THERE ("Value", "Config", ...) appears as a word HERE. That
// polluted `context` Imports/Used-by, impact, and find's PageRank blend.
// Rules: both ends inside the scan root; project code never "uses" vendored
// code by word presence (nor the reverse); names declared in too many files
// carry no signal.
inline constexpr std::size_t kXrefMaxDeclarers = 3;   // > this many decls -> skip name
inline constexpr std::size_t kXrefMinNameLen   = 3;   // shorter class names -> skip

inline std::string xrefNorm(const std::string& p) {
    std::string s = p;
    for (auto& c : s) c = c == '\\' ? '/' : (char)std::tolower((unsigned char)c);
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

// `path` under `root` (both absolute, any slash/case). Empty root = no limit.
inline bool xrefInScope(const std::string& path, const std::string& root) {
    if (root.empty()) return true;
    const std::string p = xrefNorm(path), r = xrefNorm(root);
    return p.size() > r.size() && p.compare(0, r.size(), r) == 0 && p[r.size()] == '/';
}

// Vendored / third-party tree segment anywhere in the path.
inline bool xrefVendorPath(const std::string& path) {
    const std::string p = "/" + xrefNorm(path) + "/";
    for (const char* seg : {"/third_party/", "/vendor/", "/node_modules/",
                            "/external/", "/.git/", "/.icmg/"})
        if (p.find(seg) != std::string::npos) return true;
    return false;
}

// May a word-presence "uses" edge go src -> dst?
inline bool xrefEdgeAllowed(const std::string& src, const std::string& dst,
                            const std::string& root) {
    if (!xrefInScope(src, root) || !xrefInScope(dst, root)) return false;
    return xrefVendorPath(src) == xrefVendorPath(dst);   // vendor<->vendor, project<->project
}

// Is a declared class name specific enough to imply a dependency?
inline bool xrefNameUseful(const std::string& name, std::size_t declarers) {
    return name.size() >= kXrefMinNameLen && declarers >= 1 &&
           declarers <= kXrefMaxDeclarers;
}

} // namespace icmg::graph
