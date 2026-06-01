#include "context_node_store.hpp"
#include <algorithm>
#include <sstream>
#include <chrono>
#include <cctype>
#include <cmath>

namespace icmg::core {

static int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

ContextNodeStore::ContextNodeStore(Db& db) : db_(db) {}

// ---- helpers ----

ContextNode ContextNodeStore::fromRow(const Row& r) {
    ContextNode n;
    if (r.size() > 0) try { n.id = std::stoll(r[0]); } catch (...) {}
    if (r.size() > 1) n.node_key    = r[1];
    if (r.size() > 2) n.title       = r[2];
    if (r.size() > 3) n.content     = r[3];
    if (r.size() > 4) n.source_file = r[4];
    if (r.size() > 5) n.tier        = r[5];
    if (r.size() > 6) n.tags        = r[6];
    if (r.size() > 7) try { n.active = r[7] != "0"; } catch (...) {}
    if (r.size() > 8) try { n.created_at = std::stoll(r[8]); } catch (...) {}
    if (r.size() > 9) try { n.updated_at = std::stoll(r[9]); } catch (...) {}
    return n;
}

std::vector<std::string> ContextNodeStore::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string tok;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '_') {
            tok += static_cast<char>(std::tolower(c));
        } else {
            if (tok.size() >= 2) tokens.push_back(tok);
            tok.clear();
        }
    }
    if (tok.size() >= 2) tokens.push_back(tok);
    return tokens;
}

double ContextNodeStore::scoreNode(const std::vector<std::string>& query_tokens,
                                    const ContextNode& node) {
    if (query_tokens.empty()) return 0.0;

    // Build weighted corpus: title (3×), tags (2×), content (1×)
    std::string weighted = node.title + " " + node.title + " " + node.title
                         + " " + node.tags + " " + node.tags
                         + " " + node.content;
    auto corpus_tokens = tokenize(weighted);

    // Term frequency map
    std::unordered_map<std::string, int> tf;
    for (auto& t : corpus_tokens) tf[t]++;

    double score = 0.0;
    for (auto& qt : query_tokens) {
        auto it = tf.find(qt);
        if (it != tf.end()) {
            // Saturated TF: log(1 + tf)
            score += std::log1p(static_cast<double>(it->second));
        }
    }
    // Normalize by query length
    return score / static_cast<double>(query_tokens.size());
}

// ---- public API ----

int64_t ContextNodeStore::upsert(const ContextNode& node) {
    int64_t ts = nowEpoch();
    db_.run(
        "INSERT INTO context_nodes(node_key,title,content,source_file,tier,tags,active,created_at,updated_at)"
        " VALUES(?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(node_key) DO UPDATE SET"
        "   title=excluded.title, content=excluded.content,"
        "   source_file=excluded.source_file, tier=excluded.tier,"
        "   tags=excluded.tags, active=excluded.active,"
        "   updated_at=excluded.updated_at",
        {
            node.node_key, node.title, node.content,
            node.source_file, node.tier, node.tags,
            node.active ? "1" : "0",
            std::to_string(ts), std::to_string(ts)
        }
    );
    return db_.lastInsertId();
}

std::optional<ContextNode> ContextNodeStore::get(const std::string& node_key) const {
    std::optional<ContextNode> result;
    db_.query(
        "SELECT id,node_key,title,content,source_file,tier,tags,active,created_at,updated_at"
        " FROM context_nodes WHERE node_key=? LIMIT 1",
        {node_key},
        [&](const Row& r) { result = fromRow(r); }
    );
    return result;
}

std::vector<ContextNode> ContextNodeStore::list(const std::string& tier,
                                                 bool active_only) const {
    std::vector<ContextNode> results;
    std::string sql =
        "SELECT id,node_key,title,content,source_file,tier,tags,active,created_at,updated_at"
        " FROM context_nodes WHERE 1=1";
    std::vector<std::string> params;

    if (!tier.empty()) {
        sql += " AND tier=?";
        params.push_back(tier);
    }
    if (active_only) {
        sql += " AND active=1";
    }
    sql += " ORDER BY tier,title";

    db_.query(sql, params, [&](const Row& r) {
        results.push_back(fromRow(r));
    });
    return results;
}

std::vector<ContextNode> ContextNodeStore::search(const std::string& query,
                                                   const std::string& tier_filter,
                                                   int limit,
                                                   double min_score) const {
    auto candidates = list(tier_filter, true);
    // Exclude "frozen" tier from default search — inject only on explicit request.
    if (tier_filter.empty()) {
        candidates.erase(
            std::remove_if(candidates.begin(), candidates.end(),
                [](const ContextNode& n) { return n.tier == "frozen"; }),
            candidates.end());
    }
    if (query.empty()) {
        if (limit > 0 && (int)candidates.size() > limit)
            candidates.resize(limit);
        return candidates;
    }

    auto q_tokens = tokenize(query);
    std::vector<std::pair<double, ContextNode>> scored;
    scored.reserve(candidates.size());

    for (auto& node : candidates) {
        double s = scoreNode(q_tokens, node);
        if (s >= min_score) scored.push_back({s, node});
    }

    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.first > b.first; });

    std::vector<ContextNode> out;
    int n = (limit > 0) ? std::min(limit, (int)scored.size()) : (int)scored.size();
    out.reserve(n);
    for (int i = 0; i < n; ++i) out.push_back(std::move(scored[i].second));
    return out;
}

bool ContextNodeStore::setActive(const std::string& node_key, bool active) {
    int64_t ts = nowEpoch();
    db_.run("UPDATE context_nodes SET active=?,updated_at=? WHERE node_key=?",
            {active ? "1" : "0", std::to_string(ts), node_key});
    return true;
}

bool ContextNodeStore::remove(const std::string& node_key) {
    db_.run("DELETE FROM context_nodes WHERE node_key=?", {node_key});
    return true;
}

int ContextNodeStore::count(const std::string& tier) const {
    int result = 0;
    std::string sql = "SELECT COUNT(*) FROM context_nodes";
    std::vector<std::string> params;
    if (!tier.empty()) {
        sql += " WHERE tier=?";
        params.push_back(tier);
    }
    db_.query(sql, params, [&](const Row& r) {
        if (!r.empty()) try { result = std::stoi(r[0]); } catch (...) {}
    });
    return result;
}

} // namespace icmg::core
