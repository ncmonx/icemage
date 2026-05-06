#include "graph_store.hpp"
#include <chrono>
#include <algorithm>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <tuple>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
namespace icmg::graph {

// Normalize path for DB lookup: try canonical abs path, then stored relative variants
static std::vector<std::string> pathVariants(const std::string& path) {
    std::vector<std::string> v;
    v.push_back(path);
    // Replace forward slashes with backslashes and vice versa
    std::string fwd = path, bwd = path;
    std::replace(fwd.begin(), fwd.end(), '\\', '/');
    std::replace(bwd.begin(), bwd.end(), '/', '\\');
    v.push_back(fwd);
    v.push_back(bwd);
    // Add ./ and .\ prefixes if not present
    if (path.size() < 2 || (path[0] != '.' && path[0] != '/')) {
        v.push_back("./" + fwd);
        v.push_back(".\\" + bwd);
    }
    // Try absolute path
    std::error_code ec;
    auto abs = fs::weakly_canonical(path, ec);
    if (!ec) {
        v.push_back(abs.string());
        std::string a = abs.string();
        std::replace(a.begin(), a.end(), '\\', '/');
        v.push_back(a);
    }
    return v;
}

static int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

GraphStore::GraphStore(core::Db& db) : db_(db) {}

GraphNode GraphStore::rowToNode(const core::Row& row) const {
    GraphNode n;
    if (row.size() > 0) n.id           = std::stoll(row[0]);
    if (row.size() > 1) n.path         = row[1];
    if (row.size() > 2) n.lang         = row[2];
    if (row.size() > 3) n.context      = row[3];
    if (row.size() > 4) n.symbols      = row[4];
    if (row.size() > 5) try { n.size_bytes   = std::stoll(row[5]); } catch (...) {}
    if (row.size() > 6) n.file_hash    = row[6];
    if (row.size() > 7) try { n.updated_at   = std::stoll(row[7]); } catch (...) {}
    if (row.size() > 8) try { n.access_count = std::stoll(row[8]); } catch (...) {}
    return n;
}

GraphEdge GraphStore::rowToEdge(const core::Row& row) const {
    GraphEdge e;
    if (row.size() > 0) try { e.src       = std::stoll(row[0]); } catch (...) {}
    if (row.size() > 1) try { e.dst       = std::stoll(row[1]); } catch (...) {}
    if (row.size() > 2) e.edge_type = row[2];
    if (row.size() > 3) try { e.weight    = std::stod(row[3]);  } catch (...) {}
    return e;
}

int64_t GraphStore::upsertNode(const GraphNode& node) {
    int64_t now = nowEpoch();
    db_.run(
        "INSERT INTO graph_nodes(path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count)"
        " VALUES(?,?,?,?,?,?,?,0)"
        " ON CONFLICT(path) DO UPDATE SET"
        " lang=excluded.lang, context=excluded.context, symbols=excluded.symbols,"
        " size_bytes=excluded.size_bytes, file_hash=excluded.file_hash, updated_at=excluded.updated_at",
        {node.path, node.lang, node.context, node.symbols,
         std::to_string(node.size_bytes), node.file_hash, std::to_string(now)});

    // Return existing id (ON CONFLICT doesn't change rowid)
    int64_t id = 0;
    db_.query("SELECT id FROM graph_nodes WHERE path=?", {node.path},
              [&](const core::Row& r) { if (!r.empty()) id = std::stoll(r[0]); });
    return id;
}

std::optional<GraphNode> GraphStore::getNode(const std::string& path) {
    // Try exact match first, then normalized variants
    for (auto& v : pathVariants(path)) {
        std::optional<GraphNode> result;
        db_.query(
            "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count"
            " FROM graph_nodes WHERE path=?",
            {v},
            [&](const core::Row& r) { result = rowToNode(r); });
        if (result) return result;
    }
    // Last resort: match by filename suffix
    std::optional<GraphNode> result;
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    // Extract basename
    auto pos = norm.rfind('/');
    std::string base = (pos != std::string::npos) ? norm.substr(pos + 1) : norm;
    if (!base.empty()) {
        db_.query(
            "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count"
            " FROM graph_nodes WHERE path LIKE ? OR path LIKE ?",
            {"%" + base, "%" + base},
            [&](const core::Row& r) {
                if (!result) result = rowToNode(r); // take first match
            });
    }
    return result;
}

void GraphStore::removeNode(const std::string& path) {
    // Manual cascade (SQLite foreign keys may be off)
    auto node = getNode(path);
    if (!node) return;
    db_.run("DELETE FROM graph_edges WHERE src=? OR dst=?",
            {std::to_string(node->id), std::to_string(node->id)});
    db_.run("DELETE FROM graph_nodes WHERE id=?", {std::to_string(node->id)});
}

std::vector<GraphNode> GraphStore::all() const {
    std::vector<GraphNode> result;
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count"
        " FROM graph_nodes ORDER BY path",
        {},
        [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    return result;
}

bool GraphStore::isStale(const std::string& path, const std::string& hash) const {
    bool found = false;
    bool stale = true;
    db_.query("SELECT file_hash FROM graph_nodes WHERE path=?", {path},
              [&](const core::Row& r) {
                  found = true;
                  stale = r.empty() || r[0] != hash;
              });
    return !found || stale;
}

void GraphStore::upsertEdge(const GraphEdge& edge) {
    // Skip unresolved edges (dst == -1) — FK requires valid node id
    if (edge.dst < 0) return;
    db_.run(
        "INSERT OR REPLACE INTO graph_edges(src,dst,edge_type,weight) VALUES(?,?,?,?)",
        {std::to_string(edge.src), std::to_string(edge.dst),
         edge.edge_type, std::to_string(edge.weight)});
}

std::vector<GraphEdge> GraphStore::edgesFrom(int64_t nodeId) {
    std::vector<GraphEdge> result;
    db_.query("SELECT src,dst,edge_type,weight FROM graph_edges WHERE src=?",
              {std::to_string(nodeId)},
              [&](const core::Row& r) { result.push_back(rowToEdge(r)); });
    return result;
}

std::vector<GraphEdge> GraphStore::edgesTo(int64_t nodeId) {
    std::vector<GraphEdge> result;
    db_.query("SELECT src,dst,edge_type,weight FROM graph_edges WHERE dst=?",
              {std::to_string(nodeId)},
              [&](const core::Row& r) { result.push_back(rowToEdge(r)); });
    return result;
}

// A2: single JOIN query (no N+1)
std::vector<GraphNode> GraphStore::related(const std::string& path, int limit) {
    auto node = getNode(path);
    if (!node) return {};

    bumpAccess(node->id);

    std::vector<GraphNode> result;
    db_.query(
        "SELECT r.id,r.path,r.lang,r.context,r.symbols,r.size_bytes,r.file_hash,r.updated_at,r.access_count"
        " FROM graph_edges e"
        " JOIN graph_nodes r ON r.id = e.dst"
        " WHERE e.src=?"
        " ORDER BY e.weight DESC LIMIT ?",
        {std::to_string(node->id), std::to_string(limit)},
        [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    return result;
}

std::vector<GraphNode> GraphStore::search(const std::string& query, int limit) {
    std::string pat = "%" + query + "%";
    std::vector<GraphNode> result;
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count"
        " FROM graph_nodes"
        " WHERE path LIKE ? OR context LIKE ? OR symbols LIKE ?"
        " ORDER BY updated_at DESC LIMIT ?",
        {pat, pat, pat, std::to_string(limit)},
        [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    return result;
}

// A5: BFS over inbound edges — who depends on `path`
std::vector<GraphNode> GraphStore::impact(const std::string& path, int depth) {
    auto start = getNode(path);
    if (!start) return {};

    std::vector<GraphNode> result;
    std::unordered_set<int64_t> visited;
    std::vector<std::pair<int64_t,int>> queue = {{start->id, 0}};
    visited.insert(start->id);

    while (!queue.empty()) {
        auto [nodeId, d] = queue.front();
        queue.erase(queue.begin());
        if (d >= depth) continue;

        auto inbound = edgesTo(nodeId);
        for (auto& e : inbound) {
            if (visited.count(e.src)) continue;
            visited.insert(e.src);
            db_.query(
                "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count"
                " FROM graph_nodes WHERE id=?",
                {std::to_string(e.src)},
                [&](const core::Row& r) { result.push_back(rowToNode(r)); });
            queue.push_back({e.src, d+1});
        }
    }
    return result;
}

// A4: nodes with no inbound edges
std::vector<GraphNode> GraphStore::orphans(const std::vector<std::string>& exclude_patterns) const {
    std::vector<GraphNode> result;
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count"
        " FROM graph_nodes"
        " WHERE id NOT IN (SELECT DISTINCT dst FROM graph_edges WHERE dst >= 0)"
        " ORDER BY path",
        {},
        [&](const core::Row& r) {
            auto node = rowToNode(r);
            // Exclude known entry points
            bool skip = false;
            for (auto& pat : exclude_patterns) {
                if (node.path.find(pat) != std::string::npos) { skip = true; break; }
            }
            if (!skip) result.push_back(node);
        });
    return result;
}

// A3: DFS cycle detection with WHITE/GRAY/BLACK coloring
std::vector<std::vector<std::string>> GraphStore::cycles(const std::string& lang_filter) const {
    auto nodes = all();
    // Filter by lang if needed
    if (!lang_filter.empty()) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
            [&](const GraphNode& n) { return n.lang != lang_filter; }), nodes.end());
    }

    // Build adjacency: id -> list of dst ids
    std::unordered_map<int64_t, std::vector<int64_t>> adj;
    for (auto& n : nodes) {
        db_.query("SELECT dst FROM graph_edges WHERE src=?",
                  {std::to_string(n.id)},
                  [&](const core::Row& r) {
                      if (!r.empty()) {
                          int64_t dst = std::stoll(r[0]);
                          adj[n.id].push_back(dst);
                      }
                  });
    }

    // id -> path lookup
    std::unordered_map<int64_t, std::string> id2path;
    for (auto& n : nodes) id2path[n.id] = n.path;

    enum Color { WHITE = 0, GRAY, BLACK };
    std::unordered_map<int64_t, Color> color;
    for (auto& n : nodes) color[n.id] = WHITE;

    std::vector<std::vector<std::string>> found_cycles;
    std::vector<int64_t> stack;

    std::function<void(int64_t)> dfs = [&](int64_t u) {
        color[u] = GRAY;
        stack.push_back(u);
        for (int64_t v : adj[u]) {
            if (color.count(v) == 0) continue;  // not in filtered set
            if (color[v] == GRAY) {
                // Found cycle — extract it from stack
                std::vector<std::string> cycle;
                auto it = std::find(stack.begin(), stack.end(), v);
                for (; it != stack.end(); ++it) cycle.push_back(id2path[*it]);
                cycle.push_back(id2path[v]);
                found_cycles.push_back(cycle);
            } else if (color[v] == WHITE) {
                dfs(v);
            }
        }
        stack.pop_back();
        color[u] = BLACK;
    };

    for (auto& n : nodes) {
        if (color[n.id] == WHITE) dfs(n.id);
    }
    return found_cycles;
}

// A6: Tarjan SCC
std::vector<std::vector<std::string>> GraphStore::scc() const {
    auto nodes = all();
    std::unordered_map<int64_t, std::vector<int64_t>> adj;
    for (auto& n : nodes) {
        db_.query("SELECT dst FROM graph_edges WHERE src=?",
                  {std::to_string(n.id)},
                  [&](const core::Row& r) {
                      if (!r.empty()) adj[n.id].push_back(std::stoll(r[0]));
                  });
    }
    std::unordered_map<int64_t, std::string> id2path;
    for (auto& n : nodes) id2path[n.id] = n.path;

    std::unordered_map<int64_t, int> idx, low;
    std::unordered_set<int64_t> on_stack;
    std::stack<int64_t> stk;
    int counter = 0;
    std::vector<std::vector<std::string>> result;

    std::function<void(int64_t)> strongconnect = [&](int64_t v) {
        idx[v] = low[v] = counter++;
        stk.push(v);
        on_stack.insert(v);

        for (int64_t w : adj[v]) {
            if (idx.count(w) == 0) {
                strongconnect(w);
                low[v] = std::min(low[v], low[w]);
            } else if (on_stack.count(w)) {
                low[v] = std::min(low[v], idx[w]);
            }
        }

        if (low[v] == idx[v]) {
            std::vector<std::string> component;
            int64_t w;
            do {
                w = stk.top(); stk.pop();
                on_stack.erase(w);
                component.push_back(id2path.count(w) ? id2path[w] : std::to_string(w));
            } while (w != v);
            if (component.size() > 1) result.push_back(component);
        }
    };

    for (auto& n : nodes) {
        if (idx.count(n.id) == 0) strongconnect(n.id);
    }
    return result;
}

// Helper: build lowercase normalized path segments for a file path
// Used for namespace-to-path fuzzy matching
static std::vector<std::string> pathSegs(const std::string& raw_path) {
    std::string norm = raw_path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    // Strip extension of the last segment
    auto dot = norm.rfind('.');
    auto slash = norm.rfind('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        norm = norm.substr(0, dot);
    std::vector<std::string> segs;
    std::istringstream ss(norm);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (!seg.empty()) {
            std::transform(seg.begin(), seg.end(), seg.begin(), ::tolower);
            segs.push_back(seg);
        }
    }
    return segs;
}

// A7 (2-pass): resolve collected import strings to dst node IDs, insert resolved edges.
// import_list: (src_node_id, src_file_path, import_name_string)
//
// Resolution strategies (in priority order):
//   1. Exact namespace match   — symbols JSON "namespaces" field (C#/Java)
//   2. Path suffix match       — import string is a path fragment (#include "x/y.h")
//   3. Dotted namespace suffix — last segment(s) of "A.B.C" appear in file path
void GraphStore::resolveAndInsertEdges(
    const std::vector<std::tuple<int64_t,std::string,std::string>>& import_list)
{
    if (import_list.empty()) return;

    // Build id → (path, normalized_path, path_segs) map from all nodes
    struct NodeInfo {
        int64_t id;
        std::string path;
        std::string norm;   // lowercase with forward slashes, no extension on leaf
        std::vector<std::string> segs;
    };
    std::vector<NodeInfo> nodes;
    std::unordered_map<std::string, int64_t> path2id;                    // raw path → id
    std::unordered_map<std::string, int64_t> norm2id;                    // normalized → id
    std::unordered_map<std::string, std::vector<int64_t>> ns2ids;        // namespace → all files declaring it

    db_.query("SELECT id,path,symbols FROM graph_nodes", {},
        [&](const core::Row& r) {
            if (r.size() < 2) return;
            NodeInfo ni;
            ni.id   = std::stoll(r[0]);
            ni.path = r[1];
            // Build normalized version: lowercase, forward slashes
            ni.norm = ni.path;
            std::replace(ni.norm.begin(), ni.norm.end(), '\\', '/');
            std::transform(ni.norm.begin(), ni.norm.end(), ni.norm.begin(), ::tolower);
            ni.segs = pathSegs(ni.path);
            path2id[ni.path] = ni.id;
            norm2id[ni.norm] = ni.id;
            // Also add slash-normalized original
            std::string fwd = ni.path;
            std::replace(fwd.begin(), fwd.end(), '\\', '/');
            path2id[fwd] = ni.id;

            // Parse symbols JSON for declared namespaces (multiple files can share a namespace)
            if (r.size() >= 3 && !r[2].empty()) {
                try {
                    auto j = nlohmann::json::parse(r[2]);
                    if (j.contains("namespaces")) {
                        for (auto& ns : j["namespaces"]) {
                            std::string nsstr = ns.get<std::string>();
                            if (!nsstr.empty())
                                ns2ids[nsstr].push_back(ni.id);  // all files per namespace
                        }
                    }
                } catch (...) {}
            }
            nodes.push_back(std::move(ni));
        });

    // Resolve each import and insert edges
    for (auto& [src_id, src_path, import_name] : import_list) {
        if (import_name.empty()) continue;

        // Collect all resolved destination node IDs for this import
        std::unordered_set<int64_t> resolved;

        // --- Strategy 1: Exact namespace declaration match ---
        // All files declaring the matched namespace get an edge (same namespace = same module)
        {
            auto it = ns2ids.find(import_name);
            if (it != ns2ids.end()) {
                for (int64_t nid : it->second)
                    if (nid != src_id) resolved.insert(nid);
            }
        }

        // --- Strategy 2: Exact path / path suffix match ---
        // Handles C++ #include "relative/path.hpp", JS import './module'
        // Always runs (union with strategy 1)
        {
            std::string imp_lower = import_name;
            std::replace(imp_lower.begin(), imp_lower.end(), '\\', '/');
            std::transform(imp_lower.begin(), imp_lower.end(), imp_lower.begin(), ::tolower);

            // Exact path match
            {
                auto it = path2id.find(import_name);
                if (it != path2id.end() && it->second != src_id)
                    resolved.insert(it->second);
            }
            // Suffix match: stored path ends with "/import_name"
            for (auto& ni : nodes) {
                if (ni.id == src_id) continue;
                if (ni.norm.size() >= imp_lower.size()) {
                    size_t offset = ni.norm.size() - imp_lower.size();
                    if (ni.norm.substr(offset) == imp_lower &&
                        (offset == 0 || ni.norm[offset-1] == '/')) {
                        resolved.insert(ni.id);
                        break;
                    }
                }
            }
        }

        // --- Strategy 3: Dotted namespace → path segment matching ---
        // For C# "using A.B.C", the last segment (C) MUST appear as a path
        // segment (directory or filename-without-ext) in the candidate file.
        // Always runs (union with strategies 1&2). Links to ALL matching files (cap=20).
        if (import_name.find('.') != std::string::npos) {
            std::vector<std::string> ns_parts;
            {
                std::istringstream iss(import_name);
                std::string part;
                while (std::getline(iss, part, '.')) {
                    if (!part.empty()) {
                        std::transform(part.begin(), part.end(), part.begin(), ::tolower);
                        ns_parts.push_back(part);
                    }
                }
            }
            if (!ns_parts.empty()) {
                std::string last_seg = ns_parts.back();
                int cap = 0;
                for (auto& ni : nodes) {
                    if (ni.id == src_id) continue;
                    if (cap >= 20) break;  // safety cap per import
                    // The last namespace segment MUST match a path segment
                    bool last_ok = std::find(ni.segs.begin(), ni.segs.end(), last_seg)
                                   != ni.segs.end();
                    if (!last_ok) continue;

                    // Count how many ns_parts appear ANYWHERE in path segs
                    int score = 0;
                    for (auto& nsp : ns_parts) {
                        if (std::find(ni.segs.begin(), ni.segs.end(), nsp) != ni.segs.end())
                            ++score;
                    }
                    // Accept if at least the last segment matched (score >= 1)
                    if (score >= 1) {
                        resolved.insert(ni.id);
                        ++cap;
                    }
                }
            }
        }

        // Insert one edge per resolved destination
        for (int64_t dst_id : resolved) {
            GraphEdge edge;
            edge.src       = src_id;
            edge.dst       = dst_id;
            edge.edge_type = "imports";
            edge.weight    = 1.0;
            upsertEdge(edge);
        }
    }
}

// A7: legacy incremental edge resolution pass.
// Used when new nodes are added without a full 2-pass scan.
// Queries stored edges where dst=-1 and tries to resolve them.
void GraphStore::resolveEdges() {
    // Get all nodes as path lookup
    std::unordered_map<std::string, int64_t> path2id;
    db_.query("SELECT id,path FROM graph_nodes", {},
              [&](const core::Row& r) {
                  if (r.size() >= 2) path2id[r[1]] = std::stoll(r[0]);
              });

    // Unresolved edges: dst = -1 (stored with dst_path as edge_type suffix convention)
    // For simplicity: re-run for edges where dst node path can be resolved
    db_.query("SELECT src,dst,edge_type,weight FROM graph_edges WHERE dst=-1", {},
              [&](const core::Row& r) {
                  if (r.size() < 4) return;
                  std::string etype = r[2];
                  // edge_type stores "imports:path/to/file"
                  auto colon = etype.find(':');
                  if (colon == std::string::npos) return;
                  std::string dep_path = etype.substr(colon + 1);
                  for (auto& [p, id] : path2id) {
                      if (p.find(dep_path) != std::string::npos ||
                          dep_path.find(p) != std::string::npos) {
                          db_.run("UPDATE graph_edges SET dst=?,edge_type=? WHERE src=? AND dst=-1 AND edge_type=?",
                                  {std::to_string(id), etype.substr(0, colon),
                                   r[0], r[2]});
                          break;
                      }
                  }
              });
}

// A8: record scan run
int64_t GraphStore::recordScanRun(const std::string& root, int node_count, int edge_count) {
    int64_t now = nowEpoch();
    db_.run("INSERT INTO scan_runs(root_path,node_count,edge_count,created_at) VALUES(?,?,?,?)",
            {root, std::to_string(node_count), std::to_string(edge_count), std::to_string(now)});
    int64_t id = 0;
    db_.query("SELECT last_insert_rowid()", {},
              [&](const core::Row& r) { if (!r.empty()) id = std::stoll(r[0]); });
    return id;
}

// A10: hot files by access_count in recent days
std::vector<GraphNode> GraphStore::hot(int days, int limit) const {
    int64_t cutoff = nowEpoch() - (int64_t)days * 86400;
    std::vector<GraphNode> result;
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count"
        " FROM graph_nodes"
        " WHERE updated_at > ?"
        " ORDER BY access_count DESC LIMIT ?",
        {std::to_string(cutoff), std::to_string(limit)},
        [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    return result;
}

void GraphStore::bumpAccess(int64_t nodeId) {
    db_.run("UPDATE graph_nodes SET access_count=access_count+1 WHERE id=?",
            {std::to_string(nodeId)});
}

int GraphStore::nodeCount() const {
    int count = 0;
    db_.query("SELECT COUNT(*) FROM graph_nodes", {},
              [&](const core::Row& r) { if (!r.empty()) try { count = std::stoi(r[0]); } catch (...) {} });
    return count;
}

int GraphStore::edgeCount() const {
    int count = 0;
    db_.query("SELECT COUNT(*) FROM graph_edges", {},
              [&](const core::Row& r) { if (!r.empty()) try { count = std::stoi(r[0]); } catch (...) {} });
    return count;
}

} // namespace icmg::graph
