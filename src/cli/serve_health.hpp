#pragma once
// Health-probe JSON for `icmg serve` (GET /api/health).
// Pure builder (no socket, no DB) so the status shape is unit-tested in
// isolation, same pattern as recall_index.hpp / serve dashboard helpers.
//
// Shape:
//   {"status":"ok|degraded","db_ok":true,"uptime_s":N,
//    "memory_nodes":N,"graph_nodes":N,"version":"x.y.z"}
// status = "ok" when db_ok, else "degraded" — lets a monitor poll one URL.

#include <string>
#include <sstream>

namespace icmg::cli {

inline std::string buildHealthJson(bool db_ok, long long uptime_s,
                                   long long memory_nodes, long long graph_nodes,
                                   const std::string& version) {
    std::ostringstream os;
    os << "{"
       << "\"status\":\"" << (db_ok ? "ok" : "degraded") << "\","
       << "\"db_ok\":" << (db_ok ? "true" : "false") << ","
       << "\"uptime_s\":" << uptime_s << ","
       << "\"memory_nodes\":" << memory_nodes << ","
       << "\"graph_nodes\":" << graph_nodes << ","
       << "\"version\":\"" << version << "\""
       << "}";
    return os.str();
}

} // namespace icmg::cli
