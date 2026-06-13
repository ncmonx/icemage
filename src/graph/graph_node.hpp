#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace icmg::graph {

struct GraphNode {
    int64_t     id          = 0;
    std::string path;
    std::string lang;
    std::string context;     // first doc comment / description
    std::string symbols;     // JSON: {"imports":[],"classes":[],"functions":[]}
    int64_t     size_bytes  = 0;
    std::string file_hash;   // MD5 for staleness check
    int64_t     updated_at  = 0;
    int64_t     access_count = 0;
    std::string zone        = "default";  // Phase 17: subsystem partitioning

    // Phase 18: symbol-level fields (kind='file' for legacy nodes)
    int64_t     parent_id   = 0;     // 0 = top-level file, else parent file id
    std::string kind        = "file"; // file | class | interface | struct | record | function | method | sp
    std::string symbol_name;          // empty for file kind
    std::string signature;            // declaration line for symbols
    int         line_start  = 0;
    int         line_end    = 0;
    std::string body_hash;            // for symbol-level staleness
};

struct GraphEdge {
    int64_t     src         = 0;
    int64_t     dst         = 0;        // -1 = unresolved
    std::string edge_type;              // imports|calls|inherits|includes
    double      weight      = 1.0;
};


// Max distinct definitions a name-based call may resolve to before it is
// considered too ambiguous to link. Name-only call resolution (sym2ids) cannot
// pin the real target, so a callee defined in many files (e.g. run/get/value)
// would otherwise fan out into dozens of false "calls" edges that inflate the
// centrality of unrelated files (incl. third_party that share the name).
inline constexpr size_t kMaxCallFanout = 4;

// Pick the edge targets for a name-based call: drop self, and if the name is
// too ambiguous (> kMaxCallFanout distinct defs) emit NOTHING (a few wrong
// edges are worse than none for ranking). Pure -> unit-testable.
inline std::vector<int64_t> filterCallTargets(const std::vector<int64_t>& cands,
                                              int64_t srcId,
                                              size_t maxFanout = kMaxCallFanout) {
    std::vector<int64_t> out;
    for (int64_t d : cands) if (d != srcId) out.push_back(d);
    if (out.size() > maxFanout) out.clear();   // too ambiguous -> no edge
    return out;
}
} // namespace icmg::graph
