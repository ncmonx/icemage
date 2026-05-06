#include "graph_serializer.hpp"
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <queue>

namespace icmg::viz {

GraphSerializer::GraphSerializer(core::Db& db) : db_(db) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string GraphSerializer::escJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20)  { char buf[8]; snprintf(buf,8,"\\u%04x",c); out+=buf; }
        else                out += c;
    }
    return out;
}

std::string GraphSerializer::basename(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

std::string GraphSerializer::langColor(const std::string& lang) {
    std::string l = lang;
    std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    if (l == "cpp" || l == "c" || l == "h" || l == "hpp") return "#4fc3f7";
    if (l == "python" || l == "py")                        return "#ffd54f";
    if (l == "javascript" || l == "js" || l == "ts" ||
        l == "typescript")                                  return "#aed581";
    if (l == "rust" || l == "rs")                          return "#ff8a65";
    if (l == "go")                                          return "#80cbc4";
    if (l == "java")                                        return "#ef9a9a";
    if (l == "sql" || l == "mssql" || l == "mysql" ||
        l == "postgresql" || l == "sp")                     return "#ce93d8";
    if (l == "csharp" || l == "cs")                        return "#b39ddb";
    if (l == "php")                                         return "#90caf9";
    return "#b0bec5";
}

std::string GraphSerializer::communityPalette(int idx) {
    static const char* colors[] = {
        "#ef5350","#42a5f5","#66bb6a","#ffa726","#ab47bc",
        "#26c6da","#d4e157","#ff7043","#78909c","#ec407a",
        "#26a69a","#8d6e63","#ffca28","#5c6bc0","#29b6f6"
    };
    return colors[idx % 15];
}

// ---------------------------------------------------------------------------
// Community detection (BFS connected-component on undirected graph)
// ---------------------------------------------------------------------------

void GraphSerializer::computeCommunities(VizData& data) const {
    // Build adjacency (undirected)
    std::unordered_map<int64_t, std::vector<int64_t>> adj;
    for (auto& e : data.edges) {
        adj[e.src].push_back(e.dst);
        adj[e.dst].push_back(e.src);
    }

    std::unordered_map<int64_t, int> nodeToComm;
    int commId = 0;

    for (auto& n : data.nodes) {
        if (nodeToComm.count(n.id)) continue;

        // BFS
        std::queue<int64_t> q;
        q.push(n.id);
        nodeToComm[n.id] = commId;
        while (!q.empty()) {
            int64_t cur = q.front(); q.pop();
            auto it = adj.find(cur);
            if (it == adj.end()) continue;
            for (int64_t nb : it->second) {
                if (!nodeToComm.count(nb)) {
                    nodeToComm[nb] = commId;
                    q.push(nb);
                }
            }
        }
        commId++;
    }

    // Assign colors
    std::unordered_map<int, std::string> commColors;
    for (int i = 0; i < commId; ++i) {
        std::string cstr = std::to_string(i);
        commColors[i] = communityPalette(i);
        data.community_colors[cstr] = commColors[i];
    }

    for (auto& n : data.nodes) {
        auto it = nodeToComm.find(n.id);
        n.community = (it != nodeToComm.end()) ? std::to_string(it->second) : "0";
    }
}

// ---------------------------------------------------------------------------
// serialize()
// ---------------------------------------------------------------------------

GraphSerializer::VizData GraphSerializer::serialize(
        const std::vector<std::string>& lang_filter,
        const std::string& community_filter) const {

    VizData data;

    // Load nodes
    std::unordered_set<std::string> langSet(lang_filter.begin(), lang_filter.end());

    db_.query("SELECT id,path,lang,context,symbols,size_bytes FROM graph_nodes ORDER BY id",
              {},
              [&](const core::Row& r) {
                  if (r.size() < 2) return;
                  VizNode n;
                  try { n.id = std::stoll(r[0]); } catch (...) { return; }
                  n.path       = r.size()>1 ? r[1] : "";
                  n.lang       = r.size()>2 ? r[2] : "";
                  n.context    = r.size()>3 ? r[3] : "";
                  n.symbols    = r.size()>4 ? r[4] : "{}";
                  try { n.size_bytes = std::stoll(r.size()>5 ? r[5] : "0"); } catch (...) {}
                  n.label      = basename(n.path);

                  if (!langSet.empty()) {
                      std::string lo = n.lang;
                      std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
                      bool match = false;
                      for (auto& f : lang_filter) {
                          std::string fl = f;
                          std::transform(fl.begin(), fl.end(), fl.begin(), ::tolower);
                          if (lo == fl) { match = true; break; }
                      }
                      if (!match) return;
                  }

                  data.nodes.push_back(std::move(n));
              });

    // Build node id set for edge filtering
    std::unordered_set<int64_t> nodeIds;
    for (auto& n : data.nodes) nodeIds.insert(n.id);

    // Load edges
    db_.query("SELECT src,dst,edge_type,weight FROM graph_edges", {},
              [&](const core::Row& r) {
                  if (r.size() < 3) return;
                  VizEdge e;
                  try { e.src = std::stoll(r[0]); } catch (...) { return; }
                  try { e.dst = std::stoll(r[1]); } catch (...) {}
                  e.edge_type = r[2];
                  try { e.weight = std::stod(r.size()>3 ? r[3] : "1.0"); } catch (...) {}

                  // Only include edges where both endpoints are in filtered set
                  if (!nodeIds.count(e.src) || !nodeIds.count(e.dst)) return;

                  data.edges.push_back(e);
              });

    // Compute degree
    std::unordered_map<int64_t, int> degree;
    for (auto& e : data.edges) {
        degree[e.src]++;
        degree[e.dst]++;
    }
    for (auto& n : data.nodes) {
        n.degree = degree[n.id];
    }

    // Community detection
    computeCommunities(data);

    // Community filter (post-process)
    if (!community_filter.empty()) {
        data.nodes.erase(
            std::remove_if(data.nodes.begin(), data.nodes.end(),
                           [&](const VizNode& n){ return n.community != community_filter; }),
            data.nodes.end());

        std::unordered_set<int64_t> kept;
        for (auto& n : data.nodes) kept.insert(n.id);

        data.edges.erase(
            std::remove_if(data.edges.begin(), data.edges.end(),
                           [&](const VizEdge& e){ return !kept.count(e.src) || !kept.count(e.dst); }),
            data.edges.end());
    }

    return data;
}

// ---------------------------------------------------------------------------
// toJson()
// ---------------------------------------------------------------------------

std::string GraphSerializer::toJson(const VizData& data) const {
    std::ostringstream ss;
    ss << "{\n";

    // nodes
    ss << "\"nodes\":[\n";
    for (size_t i = 0; i < data.nodes.size(); ++i) {
        const auto& n = data.nodes[i];
        if (i) ss << ",\n";
        ss << "{\"id\":" << n.id
           << ",\"path\":\"" << escJson(n.path) << "\""
           << ",\"label\":\"" << escJson(n.label) << "\""
           << ",\"lang\":\"" << escJson(n.lang) << "\""
           << ",\"context\":\"" << escJson(n.context) << "\""
           << ",\"size_bytes\":" << n.size_bytes
           << ",\"degree\":" << n.degree
           << ",\"community\":\"" << escJson(n.community) << "\""
           << ",\"color\":\"" << escJson(langColor(n.lang)) << "\""
           << "}";
    }
    ss << "\n],\n";

    // edges
    ss << "\"edges\":[\n";
    for (size_t i = 0; i < data.edges.size(); ++i) {
        const auto& e = data.edges[i];
        if (i) ss << ",\n";
        ss << "{\"src\":" << e.src
           << ",\"dst\":" << e.dst
           << ",\"type\":\"" << escJson(e.edge_type) << "\""
           << ",\"weight\":" << e.weight
           << "}";
    }
    ss << "\n],\n";

    // community_colors
    ss << "\"communities\":{\n";
    bool first = true;
    for (auto& [cid, color] : data.community_colors) {
        if (!first) ss << ",\n";
        first = false;
        ss << "\"" << cid << "\":\"" << color << "\"";
    }
    ss << "\n}\n";

    ss << "}\n";
    return ss.str();
}

} // namespace icmg::viz
