#pragma once
// v1.71.0 Graphify: pure-compute graph analytics over GraphStore data.
//
// All functions here are pure (take nodes+edges, return values) so they are
// unit-testable without a live DB. Backs `icmg graph report` (Markdown) and
// `icmg graph viz` (standalone D3 HTML). No schema migration — edge confidence
// and degree centrality are derived on the fly from existing fields.
#include "graph_node.hpp"   // GraphNode + GraphEdge
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::graph {

// --- Edge confidence -------------------------------------------------------
// Heuristic confidence [0,1] that an edge is real, derived from resolution
// state + edge type. Unresolved targets (dst<=0) are least trustworthy.
inline double edgeConfidence(const GraphEdge& e) {
    if (e.dst <= 0) return 0.30;                 // unresolved import/call
    if (e.edge_type == "inherits")  return 0.97; // structural, unambiguous
    if (e.edge_type == "includes")  return 0.93;
    if (e.edge_type == "imports")   return 0.90;
    if (e.edge_type == "calls")     return 0.70; // name-based, can collide
    double w = e.weight > 0 ? e.weight : 1.0;
    double c = 0.6 + 0.1 * (w - 1.0);
    return c < 0.3 ? 0.3 : (c > 0.95 ? 0.95 : c);
}

// --- Degree centrality -----------------------------------------------------
// in+out degree per node id. Only edges with a resolved dst count toward the
// destination's in-degree.
inline std::map<int64_t,int> degreeCentrality(const std::vector<GraphNode>& nodes,
                                               const std::vector<GraphEdge>& edges) {
    std::map<int64_t,int> deg;
    for (const auto& n : nodes) deg[n.id] = 0;
    for (const auto& e : edges) {
        if (deg.count(e.src)) deg[e.src]++;
        if (e.dst > 0 && deg.count(e.dst)) deg[e.dst]++;
    }
    return deg;
}

// --- God nodes -------------------------------------------------------------
// Outlier high-degree hubs: degree >= mean + k*stddev (k=2 default), capped to
// the top `max_out`. Returns (id,degree) sorted by degree desc. A churn here
// fans out across the codebase, so these are the nodes to watch.
inline std::vector<std::pair<int64_t,int>>
godNodes(const std::map<int64_t,int>& deg, double k = 2.0, size_t max_out = 20) {
    std::vector<std::pair<int64_t,int>> out;
    if (deg.empty()) return out;
    double sum = 0; for (auto& kv : deg) sum += kv.second;
    double mean = sum / deg.size();
    double var = 0; for (auto& kv : deg) { double d = kv.second - mean; var += d*d; }
    double sd = std::sqrt(var / deg.size());
    double thresh = mean + k * sd;
    for (auto& kv : deg) if (kv.second >= thresh && kv.second > 0) out.push_back(kv);
    std::sort(out.begin(), out.end(),
              [](auto& a, auto& b){ return a.second > b.second; });
    if (out.size() > max_out) out.resize(max_out);
    return out;
}

// --- Markdown report -------------------------------------------------------
inline std::string buildReportMd(const std::vector<GraphNode>& nodes,
                                 const std::vector<GraphEdge>& edges,
                                 const std::vector<std::pair<int64_t,int>>& gods) {
    std::map<int64_t,std::string> path;
    for (const auto& n : nodes) path[n.id] = n.path;
    size_t unresolved = 0;
    for (const auto& e : edges) if (e.dst <= 0) unresolved++;

    std::ostringstream o;
    o << "# Graph Report\n\n";
    o << "- Nodes: " << nodes.size() << "\n";
    o << "- Edges: " << edges.size() << " (" << unresolved << " unresolved)\n";
    o << "- God-nodes (high-degree hubs): " << gods.size() << "\n\n";
    o << "## God-nodes (degree-centrality outliers)\n\n";
    if (gods.empty()) {
        o << "_None — graph has no outlier hubs._\n";
    } else {
        o << "| Node | Degree |\n|---|---|\n";
        for (auto& g : gods) {
            auto it = path.find(g.first);
            o << "| " << (it != path.end() ? it->second : std::to_string(g.first))
              << " | " << g.second << " |\n";
        }
    }
    return o.str();
}

// --- D3 HTML viz -----------------------------------------------------------
// Self-contained HTML (D3 from CDN). Node radius ∝ degree, edge opacity ∝
// confidence. Returns the full document as a string.
inline std::string jsonEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            // The JSON is embedded inside an HTML <script> block, so neutralize
            // script-context sequences: a path like "</script>..." must not be
            // able to close the script element (XSS). Emit as \uXXXX.
            case '<':  o += "\\u003c"; break;
            case '>':  o += "\\u003e"; break;
            case '&':  o += "\\u0026"; break;
            default:   o += c;      break;
        }
    }
    return o;
}

inline std::string buildVizHtml(const std::vector<GraphNode>& nodes,
                                const std::vector<GraphEdge>& edges,
                                const std::map<int64_t,int>& deg) {
    std::ostringstream nj;
    nj << "[";
    bool first = true;
    for (const auto& n : nodes) {
        if (!first) nj << ","; first = false;
        int d = deg.count(n.id) ? deg.at(n.id) : 0;
        nj << "{\"id\":" << n.id
           << ",\"path\":\"" << jsonEscape(n.path) << "\""
           << ",\"deg\":" << d
           << ",\"zone\":\"" << jsonEscape(n.zone) << "\"}";
    }
    nj << "]";

    std::ostringstream ej;
    ej << "[";
    first = true;
    for (const auto& e : edges) {
        if (e.dst <= 0) continue;   // skip unresolved for layout
        if (!first) ej << ","; first = false;
        ej << "{\"source\":" << e.src << ",\"target\":" << e.dst
           << ",\"conf\":" << edgeConfidence(e) << "}";
    }
    ej << "]";

    std::ostringstream o;
    o << "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>icmg graph</title>"
         "<style>body{margin:0;background:#0d1117;color:#c9d1d9;font:13px sans-serif}"
         ".node{stroke:#0d1117;stroke-width:1.5px}.link{stroke:#30363d}"
         "text{fill:#8b949e;font-size:9px}#h{position:fixed;top:8px;left:8px}</style>"
         "<script src=\"https://d3js.org/d3.v7.min.js\"></script></head><body>"
         "<div id=\"h\">icmg graph — node size = degree, edge opacity = confidence</div>"
         "<svg width=\"100%\" height=\"100%\"></svg><script>\n";
    o << "const nodes=" << nj.str() << ";\nconst links=" << ej.str() << ";\n";
    o << R"JS(
const svg=d3.select("svg"),W=innerWidth,H=innerHeight;
const g=svg.append("g");
svg.call(d3.zoom().on("zoom",e=>g.attr("transform",e.transform)));
const sim=d3.forceSimulation(nodes)
  .force("link",d3.forceLink(links).id(d=>d.id).distance(60))
  .force("charge",d3.forceManyBody().strength(-120))
  .force("center",d3.forceCenter(W/2,H/2));
const link=g.append("g").selectAll("line").data(links).join("line")
  .attr("class","link").attr("stroke-opacity",d=>0.2+0.7*d.conf);
const color=d3.scaleOrdinal(d3.schemeCategory10);
const node=g.append("g").selectAll("circle").data(nodes).join("circle")
  .attr("class","node").attr("r",d=>4+Math.sqrt(d.deg)*2)
  .attr("fill",d=>color(d.zone))
  .call(d3.drag()
    .on("start",(e,d)=>{if(!e.active)sim.alphaTarget(.3).restart();d.fx=d.x;d.fy=d.y;})
    .on("drag",(e,d)=>{d.fx=e.x;d.fy=e.y;})
    .on("end",(e,d)=>{if(!e.active)sim.alphaTarget(0);d.fx=null;d.fy=null;}));
node.append("title").text(d=>d.path+" (deg "+d.deg+")");
sim.on("tick",()=>{
  link.attr("x1",d=>d.source.x).attr("y1",d=>d.source.y)
      .attr("x2",d=>d.target.x).attr("y2",d=>d.target.y);
  node.attr("cx",d=>d.x).attr("cy",d=>d.y);
});
)JS";
    o << "</script></body></html>\n";
    return o.str();
}

} // namespace icmg::graph
