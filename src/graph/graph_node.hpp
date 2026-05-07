#pragma once
#include <string>
#include <cstdint>

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
};

struct GraphEdge {
    int64_t     src         = 0;
    int64_t     dst         = 0;        // -1 = unresolved
    std::string edge_type;              // imports|calls|inherits|includes
    double      weight      = 1.0;
};

} // namespace icmg::graph
