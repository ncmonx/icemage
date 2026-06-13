#include "graph_store.hpp"
#include "../core/fts_query.hpp"  // v2.0.0 search snapshot (FTS)
#include <chrono>
#include <algorithm>
#include <stack>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
namespace icmg::graph {

// v1.60 F4: transaction RAII for bulk edge insertion. Without it, each
// INSERT autocommits (one fsync per row) — the dominant cost of graph
// update. Wrapping the bulk loop in one transaction batches the fsync,
// giving 3-5× on edge-heavy graphs. Synchronous + hazard-free (unlike the
// async write-queue approach considered in v1.58): reads after COMMIT see
// everything; a throw rolls back via the dtor. NOT nestable — only used in
// terminal bulk passes that never run inside another transaction.
namespace {
struct TxnGuard {
    core::Db& db;
    bool active = false;
    explicit TxnGuard(core::Db& d) : db(d) {
        try { db.run("BEGIN"); active = true; } catch (...) { active = false; }
    }
    void commit() {
        if (active) { try { db.run("COMMIT"); } catch (...) {} active = false; }
    }
    ~TxnGuard() {
        if (active) { try { db.run("COMMIT"); } catch (...) {} }
    }
};
}  // namespace

// ---- v1.21.8 (S1): in-RAM graph cache ------------------------------------

bool GraphStore::cacheEnabled() const {
    return std::getenv("ICMG_NO_GRAPH_CACHE") == nullptr;
}

void GraphStore::clearCache() {
    node_cache_.clear();
    node_cache_order_.clear();
    node_cache_iters_.clear();
}

std::optional<GraphNode> GraphStore::cacheGetNode(const std::string& path) const {
    if (!cacheEnabled()) return std::nullopt;
    auto it = node_cache_.find(path);
    if (it == node_cache_.end()) return std::nullopt;
    // v1.45.0 C2: LRU touch -- splice key to back via stored iter.
    auto iit = node_cache_iters_.find(path);
    if (iit != node_cache_iters_.end()) {
        node_cache_order_.splice(node_cache_order_.end(), node_cache_order_, iit->second);
    }
    return it->second;
}

void GraphStore::cachePutNode(const std::string& path,
                              const GraphNode& node) const {
    if (!cacheEnabled()) return;
    // FIFO eviction when capacity reached. Cheap O(1) front-pop; we keep
    // insertion order in a parallel vector. Lookup remains O(1) via map.
    // v1.45.0 C2: LRU eviction via std::list + iter map. O(1) pop_front.
    if (node_cache_.size() >= NODE_CACHE_MAX && !node_cache_order_.empty()) {
        const std::string victim = node_cache_order_.front();
        node_cache_.erase(victim);
        node_cache_iters_.erase(victim);
        node_cache_order_.pop_front();
    }
    auto [it, inserted] = node_cache_.emplace(path, node);
    if (inserted) {
        node_cache_order_.push_back(path);
        node_cache_iters_[path] = std::prev(node_cache_order_.end());
    } else {
        it->second = node;
        auto iit = node_cache_iters_.find(path);
        if (iit != node_cache_iters_.end()) {
            node_cache_order_.splice(node_cache_order_.end(), node_cache_order_, iit->second);
        }
    }
}


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
    if (row.size() > 9 && !row[9].empty()) n.zone = row[9];
    if (row.size() > 10 && !row[10].empty()) try { n.parent_id = std::stoll(row[10]); } catch (...) {}
    if (row.size() > 11 && !row[11].empty()) n.kind = row[11];
    if (row.size() > 12) n.symbol_name = row[12];
    if (row.size() > 13) n.signature   = row[13];
    if (row.size() > 14 && !row[14].empty()) try { n.line_start = std::stoi(row[14]); } catch (...) {}
    if (row.size() > 15 && !row[15].empty()) try { n.line_end   = std::stoi(row[15]); } catch (...) {}
    if (row.size() > 16) n.body_hash   = row[16];
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
    clearCache();  // v1.21.8 (S1): coarse invalidate on any node write
    int64_t now = nowEpoch();
    std::string zone = node.zone.empty() ? "default" : node.zone;
    std::string kind = node.kind.empty() ? "file" : node.kind;
    std::string parent_str = node.parent_id > 0 ? std::to_string(node.parent_id) : "";
    std::string ls = node.line_start > 0 ? std::to_string(node.line_start) : "";
    std::string le = node.line_end   > 0 ? std::to_string(node.line_end)   : "";

    // NULLIF(?, '') casts empty bound strings to SQL NULL — needed for parent_id FK
    // and for line_start/line_end which are integer columns where '' would fail typing.
    db_.run(
        "INSERT INTO graph_nodes(path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,"
        "                        zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash)"
        " VALUES(?,?,?,?,?,?,?,0, ?, NULLIF(?,''), ?, NULLIF(?,''), NULLIF(?,''), NULLIF(?,''), NULLIF(?,''), NULLIF(?,''))"
        " ON CONFLICT(path) DO UPDATE SET"
        " lang=excluded.lang, context=excluded.context, symbols=excluded.symbols,"
        " size_bytes=excluded.size_bytes, file_hash=excluded.file_hash, updated_at=excluded.updated_at,"
        " zone=excluded.zone, parent_id=excluded.parent_id, kind=excluded.kind,"
        " symbol_name=excluded.symbol_name, signature=excluded.signature,"
        " line_start=excluded.line_start, line_end=excluded.line_end, body_hash=excluded.body_hash",
        {node.path, node.lang, node.context, node.symbols,
         std::to_string(node.size_bytes), node.file_hash, std::to_string(now), zone,
         parent_str, kind, node.symbol_name, node.signature, ls, le, node.body_hash});

    int64_t id = 0;
    db_.query("SELECT id FROM graph_nodes WHERE path=?", {node.path},
              [&](const core::Row& r) { if (!r.empty()) id = std::stoll(r[0]); });

    // v1.59 F3: keep bloom incrementally in sync once seeded (no false neg).
    if (bloom_seeded_) bloomAddPath(node.path);
    return id;
}

// v1.59 F3: bloom negative-lookup helpers.
bool GraphStore::bloomEnabled() const {
    return std::getenv("ICMG_NO_GRAPH_BLOOM") == nullptr;
}

void GraphStore::bloomAddPath(const std::string& path) const {
    node_bloom_.add(path);
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    auto pos = norm.rfind('/');
    std::string base = (pos != std::string::npos) ? norm.substr(pos + 1) : norm;
    if (!base.empty() && base != path) node_bloom_.add(base);
}

void GraphStore::ensureBloomSeeded() const {
    if (bloom_seeded_) return;
    // Size for current node count; seed full path + basename of every node.
    int64_t n = 0;
    db_.query("SELECT COUNT(*) FROM graph_nodes", {},
              [&](const core::Row& r) { if (!r.empty()) n = std::stoll(r[0]); });
    node_bloom_.reset(static_cast<std::size_t>(n > 0 ? n : 1024));
    db_.query("SELECT path FROM graph_nodes", {},
              [&](const core::Row& r) { if (!r.empty()) bloomAddPath(r[0]); });
    node_bloom_.markBuilt();
    bloom_seeded_ = true;
}

std::optional<GraphNode> GraphStore::getNode(const std::string& path) {
    // v1.21.8 (S1): cache hit short-circuits all DB roundtrips. Cache key
    // is the input path verbatim (variants are derived deterministically,
    // so a hit for the original path is sufficient).
    if (auto cached = cacheGetNode(path)) return cached;

    // Extract basename once (used by suffix fallback + bloom gate).
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    auto pos = norm.rfind('/');
    std::string base = (pos != std::string::npos) ? norm.substr(pos + 1) : norm;

    // v1.59 F3: bloom negative-lookup gate. Filter is seeded from every
    // node's full path AND basename. getNode matches only via (a) an exact
    // path variant or (b) the path-component suffix below (stored basename
    // == query basename). So if NONE of the path variants AND the basename
    // are in the bloom, no row can match — return nullopt, no SQL.
    if (bloomEnabled()) {
        ensureBloomSeeded();
        if (node_bloom_.built()) {
            bool any_maybe = (!base.empty() && node_bloom_.maybeContains(base));
            if (!any_maybe) {
                for (auto& v : pathVariants(path)) {
                    if (node_bloom_.maybeContains(v)) { any_maybe = true; break; }
                }
            }
            if (!any_maybe) return std::nullopt;   // definitely absent
        }
    }

    // Try exact match first, then normalized variants
    for (auto& v : pathVariants(path)) {
        std::optional<GraphNode> result;
        db_.query(
            "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
            " FROM graph_nodes WHERE path=?",
            {v},
            [&](const core::Row& r) { result = rowToNode(r); });
        if (result) {
            cachePutNode(path, *result);
            return result;
        }
    }
    // Last resort: match by filename component (path-boundary aware).
    // v1.59: tightened from over-broad `path LIKE %base` (matched 'xbase'
    // too) to path-component match (`%/base`, `%\base`, or exact `=base`),
    // mirroring findByBasename. Makes the bloom gate above sound: a stored
    // path matches only when its basename equals the query basename.
    std::optional<GraphNode> result;
    if (!base.empty()) {
        db_.query(
            "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
            " FROM graph_nodes WHERE path LIKE ? OR path LIKE ? OR path = ?",
            {"%/" + base, "%\\" + base, base},
            [&](const core::Row& r) {
                if (!result) result = rowToNode(r); // take first match
            });
    }
    if (result) cachePutNode(path, *result);  // v1.21.8 (S1)
    return result;
}

// v1.29.0 #10: enumerate all file-level graph_nodes whose path basename
// matches the input. Used by `icmg context` to detect ambiguous lookups
// (e.g. "header.tsx" matching 19 candidates across the project).
std::vector<GraphNode> GraphStore::findByBasename(const std::string& basename) {
    std::vector<GraphNode> out;
    if (basename.empty()) return out;
    std::string fwd = "%/" + basename;
    std::string bwd = "%\\" + basename;
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,"
        "access_count,zone,parent_id,kind,symbol_name,signature,line_start,"
        "line_end,body_hash"
        " FROM graph_nodes WHERE (path LIKE ? OR path LIKE ? OR path = ?) "
        " AND (kind IS NULL OR kind='file' OR kind='')",
        {fwd, bwd, basename},
        [&](const core::Row& r) { out.push_back(rowToNode(r)); });
    return out;
}

std::vector<GraphNode> GraphStore::childrenOf(int64_t parent_id) {
    std::vector<GraphNode> out;
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
        " FROM graph_nodes WHERE parent_id=? ORDER BY line_start",
        {std::to_string(parent_id)},
        [&](const core::Row& r) { out.push_back(rowToNode(r)); });
    return out;
}

std::vector<GraphNode> GraphStore::findSymbol(const std::string& name) {
    std::vector<GraphNode> out;
    // Phase 60: exact match first, then case-insensitive + suffix-match fallback.
    // JS/TS extractor sometimes stores qualified names (`Class.method`,
    // `module::sym`); a bare lookup of `method` should still hit. Also
    // matches when stored name is `default` or has trailing punctuation.
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
        " FROM graph_nodes WHERE symbol_name=?",
        {name},
        [&](const core::Row& r) { out.push_back(rowToNode(r)); });
    if (!out.empty()) return out;

    // Fallback 1: case-insensitive exact.
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
        " FROM graph_nodes WHERE LOWER(symbol_name)=LOWER(?)",
        {name},
        [&](const core::Row& r) { out.push_back(rowToNode(r)); });
    if (!out.empty()) return out;

    // Fallback 2: suffix match (e.g. `method` matches `Class.method`).
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
        " FROM graph_nodes"
        " WHERE symbol_name LIKE ? OR symbol_name LIKE ? OR symbol_name LIKE ?"
        " LIMIT 50",
        {std::string("%.") + name, std::string("%::") + name, std::string("%/") + name},
        [&](const core::Row& r) { out.push_back(rowToNode(r)); });
    if (!out.empty()) return out;

    // Phase 63: skip noisy substring + JSON-array fallbacks for short queries
    // (1-2 chars match nearly everything, drowns useful results).
    if (name.size() < 3) return out;

    // Fallback 3: substring match (last resort, capped).
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
        " FROM graph_nodes WHERE symbol_name LIKE ? LIMIT 20",
        {std::string("%") + name + "%"},
        [&](const core::Row& r) { out.push_back(rowToNode(r)); });
    if (!out.empty()) return out;

    // Phase 61: Fallback 4 — JS/TS file nodes store function/class names as
    // JSON array in `symbols` column (not separate child rows like C# does).
    // `["BackupTab","handleSave",...]` → match via JSON-token boundary.
    // Returns the FILE node, signaling "the symbol is defined inside this file".
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
        " FROM graph_nodes"
        " WHERE kind='file' AND ("
        "    symbols LIKE ? OR symbols LIKE ? OR symbols LIKE ? OR symbols LIKE ?"
        " ) LIMIT 30",
        {std::string("%\"") + name + "\"%",                      // ["foo","name",...]
         std::string("%:\"") + name + "\"%",                     // {"key":"name"}
         std::string("%[\"") + name + "\"%",                     // ["name",...]
         std::string("%,\"") + name + "\"%"},                    // [...,"name"]
        [&](const core::Row& r) { out.push_back(rowToNode(r)); });
    return out;
}

void GraphStore::removeSymbolsOf(int64_t parent_id) {
    clearCache();  // v1.21.8 (S1)
    db_.run("DELETE FROM graph_nodes WHERE parent_id=?", {std::to_string(parent_id)});
}

// Phase 21 hotfix: groups by lower(path), picks an upper-case-drive variant
// (or first id) as keeper, reparents edges, deletes duplicates, then
// uppercases the drive letter on the keeper's path so future scans match.
// Idempotent: returns 0 when no dups remain.
int GraphStore::dedupeCaseMixedPaths() {
    struct Row2 { int64_t id; std::string path; };
    std::unordered_map<std::string, std::vector<Row2>> buckets;
    db_.query("SELECT id, path FROM graph_nodes WHERE kind='file' ORDER BY id", {},
              [&](const core::Row& r) {
                  if (r.size() < 2) return;
                  Row2 x;
                  try { x.id = std::stoll(r[0]); } catch (...) { return; }
                  x.path = r[1];
                  std::string lower = x.path;
                  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                  buckets[lower].push_back(std::move(x));
              });

    int merged = 0;
    for (auto& [lower, rows] : buckets) {
        if (rows.size() <= 1) {
            // Even if no dup, still uppercase the drive letter on Windows-style paths
            // so subsequent rescans (which always upper-case) match this row.
            auto& only = rows[0];
            if (only.path.size() >= 2 && only.path[1] == ':' &&
                only.path[0] >= 'a' && only.path[0] <= 'z') {
                std::string up = only.path;
                up[0] = (char)(up[0] - 'a' + 'A');
                // Skip if collision (shouldn't happen — bucket key is lower-case)
                db_.run("UPDATE OR IGNORE graph_nodes SET path=? WHERE id=?",
                        {up, std::to_string(only.id)});
            }
            continue;
        }
        // Pick keeper: prefer upper-case-drive; else first id
        int64_t keeper_id = rows[0].id;
        std::string keeper_path = rows[0].path;
        for (auto& r : rows) {
            if (r.path.size() >= 2 && r.path[1] == ':' &&
                r.path[0] >= 'A' && r.path[0] <= 'Z') {
                keeper_id = r.id;
                keeper_path = r.path;
                break;
            }
        }
        // Reparent edges + delete dups
        for (auto& r : rows) {
            if (r.id == keeper_id) continue;
            ++merged;
            db_.run("UPDATE OR IGNORE graph_edges SET src=? WHERE src=?",
                    {std::to_string(keeper_id), std::to_string(r.id)});
            db_.run("UPDATE OR IGNORE graph_edges SET dst=? WHERE dst=?",
                    {std::to_string(keeper_id), std::to_string(r.id)});
            db_.run("DELETE FROM graph_edges WHERE src=dst", {});
            // Reparent symbol children to keeper
            db_.run("UPDATE OR IGNORE graph_nodes SET parent_id=? WHERE parent_id=?",
                    {std::to_string(keeper_id), std::to_string(r.id)});
            db_.run("DELETE FROM graph_nodes WHERE id=?", {std::to_string(r.id)});
        }
        // Normalize keeper's drive case
        if (keeper_path.size() >= 2 && keeper_path[1] == ':' &&
            keeper_path[0] >= 'a' && keeper_path[0] <= 'z') {
            std::string up = keeper_path;
            up[0] = (char)(up[0] - 'a' + 'A');
            db_.run("UPDATE OR IGNORE graph_nodes SET path=? WHERE id=?",
                    {up, std::to_string(keeper_id)});
        }
    }
    return merged;
}

// Phase 22: BFS forward (src→dst) or reverse (dst→src) closure with cycle detection.
std::vector<int64_t> GraphStore::closure(int64_t start,
                                          const std::vector<std::string>& edge_types,
                                          int max_depth,
                                          bool reverse) {
    std::unordered_set<int64_t> visited;
    std::vector<int64_t> order;
    std::deque<std::pair<int64_t,int>> q;
    q.push_back({start, 0});
    visited.insert(start);

    // Build edge-type filter clause (empty → all types)
    std::string type_clause;
    std::vector<std::string> params;
    if (!edge_types.empty()) {
        type_clause = " AND edge_type IN (";
        for (size_t i = 0; i < edge_types.size(); ++i) {
            if (i) type_clause += ",";
            type_clause += "?";
            params.push_back(edge_types[i]);
        }
        type_clause += ")";
    }
    std::string col_pick = reverse ? "src" : "dst";
    std::string col_where = reverse ? "dst" : "src";

    while (!q.empty()) {
        auto [cur, d] = q.front(); q.pop_front();
        if (d >= max_depth) continue;

        // SELECT <other> FROM graph_edges WHERE <pivot>=? [AND edge_type IN (...)]
        std::string sql = "SELECT " + col_pick + " FROM graph_edges WHERE "
                        + col_where + "=?" + type_clause;
        std::vector<std::string> bind = { std::to_string(cur) };
        bind.insert(bind.end(), params.begin(), params.end());

        std::vector<int64_t> neighbors;
        db_.query(sql, bind, [&](const core::Row& r){
            if (!r.empty()) try { neighbors.push_back(std::stoll(r[0])); } catch (...) {}
        });
        for (int64_t nb : neighbors) {
            if (visited.insert(nb).second) {
                order.push_back(nb);
                q.push_back({nb, d + 1});
            }
        }
    }
    return order;
}

std::vector<std::string> GraphStore::shortestPath(
    const std::string& from, const std::string& to,
    const std::vector<std::string>& edge_types, int max_depth)
{
    auto src = getNode(from);
    auto dst = getNode(to);
    if (!src || !dst) return {};
    if (src->id == dst->id) return {from};

    std::string type_clause;
    std::vector<std::string> params;
    if (!edge_types.empty()) {
        type_clause = " AND edge_type IN (";
        for (size_t i = 0; i < edge_types.size(); ++i) {
            if (i) type_clause += ",";
            type_clause += "?";
            params.push_back(edge_types[i]);
        }
        type_clause += ")";
    }

    std::unordered_map<int64_t, int64_t> parent;
    std::deque<std::pair<int64_t,int>> q;
    q.push_back({src->id, 0});
    parent[src->id] = -1;
    bool found = false;

    while (!q.empty() && !found) {
        auto [cur, d] = q.front(); q.pop_front();
        if (d >= max_depth) continue;
        std::string sql = "SELECT dst FROM graph_edges WHERE src=?" + type_clause;
        std::vector<std::string> bind = {std::to_string(cur)};
        bind.insert(bind.end(), params.begin(), params.end());
        db_.query(sql, bind, [&](const core::Row& r) {
            if (r.empty() || found) return;
            int64_t nb; try { nb = std::stoll(r[0]); } catch (...) { return; }
            if (parent.count(nb)) return;
            parent[nb] = cur;
            if (nb == dst->id) { found = true; return; }
            q.push_back({nb, d + 1});
        });
    }
    if (!found) return {};

    std::vector<int64_t> ids;
    int64_t cur = dst->id;
    while (cur != -1) { ids.push_back(cur); cur = parent.at(cur); }
    std::reverse(ids.begin(), ids.end());

    std::vector<std::string> result;
    for (int64_t id : ids) {
        db_.query("SELECT path FROM graph_nodes WHERE id=?", {std::to_string(id)},
                  [&](const core::Row& r) { if (!r.empty()) result.push_back(r[0]); });
    }
    return result;
}

std::vector<std::vector<GraphNode>> GraphStore::closureByLevel(
    int64_t start, const std::vector<std::string>& edge_types,
    int max_depth, bool reverse)
{
    std::string type_clause;
    std::vector<std::string> params;
    if (!edge_types.empty()) {
        type_clause = " AND edge_type IN (";
        for (size_t i = 0; i < edge_types.size(); ++i) {
            if (i) type_clause += ",";
            type_clause += "?";
            params.push_back(edge_types[i]);
        }
        type_clause += ")";
    }
    std::string col_pick  = reverse ? "src" : "dst";
    std::string col_where = reverse ? "dst" : "src";

    std::unordered_set<int64_t> visited;
    visited.insert(start);
    std::vector<int64_t> frontier = {start};
    std::vector<std::vector<GraphNode>> levels;

    for (int d = 0; d < max_depth && !frontier.empty(); ++d) {
        std::vector<int64_t> next;
        std::vector<GraphNode> level_nodes;
        for (int64_t c : frontier) {
            std::string sql = "SELECT " + col_pick + " FROM graph_edges WHERE "
                            + col_where + "=?" + type_clause;
            std::vector<std::string> bind = {std::to_string(c)};
            bind.insert(bind.end(), params.begin(), params.end());
            db_.query(sql, bind, [&](const core::Row& r) {
                if (r.empty()) return;
                int64_t nb; try { nb = std::stoll(r[0]); } catch (...) { return; }
                if (!visited.insert(nb).second) return;
                next.push_back(nb);
                db_.query(
                    "SELECT id,path,lang,context,symbols,size_bytes,file_hash,"
                    "updated_at,access_count,zone,parent_id,kind,symbol_name,"
                    "signature,line_start,line_end,body_hash FROM graph_nodes WHERE id=?",
                    {std::to_string(nb)},
                    [&](const core::Row& nr) { level_nodes.push_back(rowToNode(nr)); });
            });
        }
        if (!level_nodes.empty()) levels.push_back(std::move(level_nodes));
        frontier = std::move(next);
    }
    return levels;
}

std::vector<GraphNode> GraphStore::commonAncestors(
    const std::string& a, const std::string& b, int max_depth)
{
    auto na = getNode(a);
    auto nb = getNode(b);
    if (!na || !nb) return {};

    auto a_ids = closure(na->id, {}, max_depth, /*reverse=*/true);
    std::unordered_set<int64_t> a_set(a_ids.begin(), a_ids.end());

    auto b_ids = closure(nb->id, {}, max_depth, /*reverse=*/true);

    std::vector<GraphNode> result;
    for (int64_t id : b_ids) {
        if (!a_set.count(id)) continue;
        db_.query(
            "SELECT id,path,lang,context,symbols,size_bytes,file_hash,"
            "updated_at,access_count,zone,parent_id,kind,symbol_name,"
            "signature,line_start,line_end,body_hash FROM graph_nodes WHERE id=?",
            {std::to_string(id)},
            [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    }
    return result;
}

std::vector<GraphNode> GraphStore::impactAll(
    const std::vector<std::string>& paths, int depth)
{
    std::unordered_set<int64_t> visited;
    std::deque<std::pair<int64_t,int>> q;
    for (auto& p : paths) {
        auto n = getNode(p);
        if (!n) continue;
        if (visited.insert(n->id).second) q.push_back({n->id, 0});
    }
    std::vector<GraphNode> result;
    while (!q.empty()) {
        auto [cur, d] = q.front(); q.pop_front();
        if (d >= depth) continue;
        db_.query("SELECT src FROM graph_edges WHERE dst=?", {std::to_string(cur)},
                  [&](const core::Row& r) {
                      if (r.empty()) return;
                      int64_t nb; try { nb = std::stoll(r[0]); } catch (...) { return; }
                      if (!visited.insert(nb).second) return;
                      db_.query(
                          "SELECT id,path,lang,context,symbols,size_bytes,file_hash,"
                          "updated_at,access_count,zone,parent_id,kind,symbol_name,"
                          "signature,line_start,line_end,body_hash FROM graph_nodes WHERE id=?",
                          {std::to_string(nb)},
                          [&](const core::Row& nr) { result.push_back(rowToNode(nr)); });
                      q.push_back({nb, d + 1});
                  });
    }
    return result;
}

void GraphStore::removeNode(const std::string& path) {
    // v1.23.0 fix: getNode below re-populates cache. clearCache MUST run AFTER
    // the DB delete, not before, otherwise the just-cached row survives the
    // delete and getNode() callers see stale data.
    auto node = getNode(path);
    if (!node) return;
    // Manual cascade (SQLite foreign keys may be off)
    db_.run("DELETE FROM graph_edges WHERE src=? OR dst=?",
            {std::to_string(node->id), std::to_string(node->id)});
    db_.run("DELETE FROM graph_nodes WHERE id=?", {std::to_string(node->id)});
    clearCache();  // v1.21.8 (S1) — final invalidate so callers see fresh state
}

std::vector<GraphNode> GraphStore::all() const {
    std::vector<GraphNode> result;
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
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
    // v2.0.0 search snapshot: FTS5 fast-path (graph_fts) when present — O(matches)
    // via MATCH instead of an O(n) LIKE scan. Falls back to LIKE below if the
    // graph_fts virtual table is absent (old DB / FTS5 not compiled) or empty.
    {
        bool hasFts = false;
        db_.query("SELECT 1 FROM sqlite_master WHERE type='table' AND name='graph_fts'",
                  {}, [&](const core::Row&){ hasFts = true; });
        std::string fts = core::ftsQuery(query);
        if (hasFts && !fts.empty()) {
            db_.query(
                "SELECT g.id,g.path,g.lang,g.context,g.symbols,g.size_bytes,g.file_hash,"
                "g.updated_at,g.access_count,g.zone,g.parent_id,g.kind,g.symbol_name,"
                "g.signature,g.line_start,g.line_end,g.body_hash"
                " FROM graph_fts f JOIN graph_nodes g ON g.id = f.rowid"
                " WHERE graph_fts MATCH ? ORDER BY bm25(graph_fts) LIMIT ?",
                {fts, std::to_string(limit)},
                [&](const core::Row& r) { result.push_back(rowToNode(r)); });
            if (!result.empty()) return result;
        }
    }
    // Phase 60: include symbol_name in search columns. Two-tier graph
    // (file + child symbols) means searching content alone misses symbol-
    // level matches that live in their own row with empty context.
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
        " FROM graph_nodes"
        " WHERE path LIKE ? OR context LIKE ? OR symbols LIKE ?"
        "    OR symbol_name LIKE ? OR signature LIKE ?"
        " ORDER BY"
        "   CASE WHEN symbol_name = ? THEN 0"
        "        WHEN symbol_name LIKE ? THEN 1"
        "        ELSE 2 END,"
        "   updated_at DESC LIMIT ?",
        {pat, pat, pat, pat, pat,
         query, std::string("%") + query,
         std::to_string(limit)},
        [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    return result;
}

// A5: BFS over inbound edges — who depends on `path`
std::vector<GraphNode> GraphStore::impact(const std::string& path, int depth,
                                           const std::vector<std::string>& edge_types) {
    auto node = getNode(path);
    if (!node) return {};
    auto ids = closure(node->id, edge_types, depth, /*reverse=*/true);
    std::vector<GraphNode> result;
    for (int64_t id : ids) {
        db_.query(
            "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
            " FROM graph_nodes WHERE id=?",
            {std::to_string(id)},
            [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    }
    return result;
}

// A4: nodes with no inbound edges
std::vector<GraphNode> GraphStore::orphans(const std::vector<std::string>& exclude_patterns) const {
    std::vector<GraphNode> result;
    db_.query(
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
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

    // v1.60 F4: batch all edge INSERTs in one transaction (3-5× on large
    // graphs). Commits on scope exit (RAII) — every return path is covered.
    TxnGuard _txn(db_);

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

    // Phase 18: pre-build symbol_name → ids map for call/extends edges.
    // Lazily populated on first encountered call: prefix.
    std::unordered_map<std::string, std::vector<int64_t>> sym2ids;
    bool sym_loaded = false;
    auto loadSymbolMap = [&]() {
        if (sym_loaded) return;
        sym_loaded = true;
        db_.query("SELECT id, symbol_name FROM graph_nodes WHERE symbol_name IS NOT NULL AND symbol_name != ''", {},
            [&](const core::Row& r) {
                if (r.size() < 2) return;
                try { sym2ids[r[1]].push_back(std::stoll(r[0])); } catch (...) {}
            });
    };

    // Resolve each import and insert edges
    for (auto& [src_id, src_path, import_name] : import_list) {
        if (import_name.empty()) continue;

        // Phase 18: symbol-level edges via prefix tagging
        if (import_name.compare(0, 5, "call:") == 0) {
            loadSymbolMap();
            std::string callee = import_name.substr(5);
            auto it = sym2ids.find(callee);
            if (it != sym2ids.end()) {
                // Ambiguity guard (filterCallTargets): a callee name defined in
                // many files cannot be pinned by name alone -> emit edges only
                // when specific enough, else none. Kills common-name fan-out
                // that inflated unrelated/third_party centrality.
                for (int64_t dst : filterCallTargets(it->second, src_id)) {
                    GraphEdge e; e.src = src_id; e.dst = dst; e.edge_type = "calls"; e.weight = 1.5;
                    upsertEdge(e);
                }
            }
            continue;
        }
        if (import_name.compare(0, 4, "ext:") == 0) {
            loadSymbolMap();
            std::string base = import_name.substr(4);
            auto it = sym2ids.find(base);
            if (it != sym2ids.end()) {
                for (int64_t dst : it->second) {
                    if (dst == src_id) continue;
                    GraphEdge e; e.src = src_id; e.dst = dst; e.edge_type = "extends"; e.weight = 2.0;
                    upsertEdge(e);
                }
            }
            continue;
        }

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

// Strategy 4: class cross-reference (Graphify-style).
// Scans ALL nodes in DB; for each file, checks if class names declared in
// OTHER files appear as word tokens in its content. Always called after scan,
// independent of whether any imports were collected.
void GraphStore::buildXRefEdges() {
    TxnGuard _txn(db_);   // v1.60 F4: batch xref edge inserts
    // Collect all nodes: id + path
    struct NodeInfo { int64_t id; std::string path; };
    std::vector<NodeInfo> nodes;
    db_.query("SELECT id,path FROM graph_nodes", {},
        [&](const core::Row& r) {
            if (r.size() >= 2) {
                NodeInfo ni;
                ni.id   = std::stoll(r[0]);
                ni.path = r[1];
                nodes.push_back(std::move(ni));
            }
        });
    if (nodes.empty()) return;

    // Build class_name → [declaring_node_ids] map from symbols JSON
    std::unordered_map<std::string, std::vector<int64_t>> class2nodes;
    db_.query("SELECT id,symbols FROM graph_nodes", {},
        [&](const core::Row& r) {
            if (r.size() < 2 || r[1].empty()) return;
            int64_t nid = std::stoll(r[0]);
            try {
                auto j = nlohmann::json::parse(r[1]);
                if (j.contains("classes")) {
                    for (auto& cls : j["classes"]) {
                        std::string cn = cls.get<std::string>();
                        if (cn.size() >= 2)   // skip single-char names
                            class2nodes[cn].push_back(nid);
                    }
                }
            } catch (...) {}
        });
    if (class2nodes.empty()) return;

    // Helper: check if word `name` appears as a token in `content`
    auto isWordPresent = [](const std::string& content, const std::string& name) -> bool {
        size_t pos = 0;
        while ((pos = content.find(name, pos)) != std::string::npos) {
            bool pre_ok  = (pos == 0) || (!std::isalnum((unsigned char)content[pos-1])
                                          && content[pos-1] != '_');
            bool post_ok = (pos + name.size() >= content.size())
                        || (!std::isalnum((unsigned char)content[pos + name.size()])
                            && content[pos + name.size()] != '_');
            if (pre_ok && post_ok) return true;
            pos += name.size();
        }
        return false;
    };

    // For each node: read file, check class names from OTHER files
    for (auto& ni : nodes) {
        std::ifstream f(ni.path, std::ios::binary);
        if (!f) continue;
        std::ostringstream buf;
        buf << f.rdbuf();
        std::string content = buf.str();
        if (content.empty()) continue;

        for (auto& [cls_name, declaring_ids] : class2nodes) {
            for (int64_t dec_id : declaring_ids) {
                if (dec_id == ni.id) continue;  // no self-edges
                if (isWordPresent(content, cls_name)) {
                    GraphEdge edge;
                    edge.src       = ni.id;
                    edge.dst       = dec_id;
                    edge.edge_type = "uses";
                    edge.weight    = 1.5;
                    upsertEdge(edge);
                }
            }
        }
    }
}

// Visual Studio designer file grouping.
//
// For each .cs file that is NOT itself a .Designer.cs, look for:
//   <basename>.Designer.cs  — code-behind generated file
//   <basename>.resx         — resource file
// in the same directory.  If both companions exist in graph_nodes, assign all
// three the same group_id (= canonical .cs path) and insert bidirectional
// "companion" edges so traversal algorithms treat them as one unit.
void GraphStore::groupDesignerTriples() {
    // Build path → id map for fast lookups
    std::unordered_map<std::string, int64_t> path2id;
    // Also collect all .cs paths (only non-Designer ones)
    std::vector<std::pair<std::string, int64_t>> cs_nodes;   // (path, id)

    db_.query("SELECT id,path FROM graph_nodes", {},
        [&](const core::Row& r) {
            if (r.size() < 2) return;
            int64_t nid = std::stoll(r[0]);
            const std::string& p = r[1];

            // Normalize separators for key lookup
            std::string norm = p;
            std::replace(norm.begin(), norm.end(), '\\', '/');
            path2id[norm] = nid;
            path2id[p]    = nid;   // also store original for exact match

            // .cs but NOT .Designer.cs
            std::string lower = norm;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.size() > 3 && lower.substr(lower.size() - 3) == ".cs"
                && !(lower.size() > 12
                     && lower.substr(lower.size() - 12) == ".designer.cs")) {
                cs_nodes.emplace_back(p, nid);
            }
        });

    if (cs_nodes.empty()) return;

    namespace fs = std::filesystem;

    for (auto& [cs_path, cs_id] : cs_nodes) {
        // Derive base (strip .cs extension)
        fs::path p(cs_path);
        std::string stem = p.stem().string();          // e.g. "Form1"
        fs::path dir     = p.parent_path();

        // Build companion paths — try both separators
        auto findNode = [&](const std::string& filename) -> int64_t {
            fs::path full = dir / filename;
            std::string s1 = full.string();
            std::string s2 = s1;
            std::replace(s2.begin(), s2.end(), '\\', '/');
            auto it = path2id.find(s1);
            if (it != path2id.end()) return it->second;
            it = path2id.find(s2);
            if (it != path2id.end()) return it->second;
            return -1;
        };

        int64_t designer_id = findNode(stem + ".Designer.cs");
        int64_t resx_id     = findNode(stem + ".resx");

        // Need at least one companion to form a group
        if (designer_id < 0 && resx_id < 0) continue;

        // Assign group_id = canonical .cs path to all trio members
        db_.run("UPDATE graph_nodes SET group_id=? WHERE id=?",
                {cs_path, std::to_string(cs_id)});
        if (designer_id >= 0)
            db_.run("UPDATE graph_nodes SET group_id=? WHERE id=?",
                    {cs_path, std::to_string(designer_id)});
        if (resx_id >= 0)
            db_.run("UPDATE graph_nodes SET group_id=? WHERE id=?",
                    {cs_path, std::to_string(resx_id)});

        // Insert companion edges (bidirectional: canonical ↔ each companion)
        auto addCompanion = [&](int64_t a, int64_t b) {
            GraphEdge e;
            e.edge_type = "companion";
            e.weight    = 2.0;   // highest weight — tightly coupled
            e.src = a; e.dst = b; upsertEdge(e);
            e.src = b; e.dst = a; upsertEdge(e);
        };
        if (designer_id >= 0) addCompanion(cs_id, designer_id);
        if (resx_id >= 0)     addCompanion(cs_id, resx_id);
        if (designer_id >= 0 && resx_id >= 0)
            addCompanion(designer_id, resx_id);
    }
}

// A7: legacy incremental edge resolution pass.
// Used when new nodes are added without a full 2-pass scan.
// Queries stored edges where dst=-1 and tries to resolve them.
void GraphStore::resolveEdges() {
    TxnGuard _txn(db_);   // v1.60 F4: batch incremental edge resolution
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
        "SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count,zone,parent_id,kind,symbol_name,signature,line_start,line_end,body_hash"
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
