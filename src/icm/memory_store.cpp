#include "memory_store.hpp"
#include "scorer.hpp"
#include "../core/hook_bus.hpp"
#include <chrono>
#include <algorithm>
#include <sstream>
#include <cmath>

namespace icmg::icm {

// ---- helpers ----

int64_t MemoryStore::nowEpoch() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

MemoryNode MemoryStore::rowToNode(const core::Row& row) const {
    // Columns: id, topic, content, keywords, importance, frequency,
    //          last_used, created_at, expires_at, deleted_at
    MemoryNode n;
    if (row.size() > 0) n.id          = std::stoll(row[0]);
    if (row.size() > 1) n.topic       = row[1];
    if (row.size() > 2) n.content     = row[2];
    if (row.size() > 3) n.keywords    = row[3];
    if (row.size() > 4) try { n.importance = std::stoi(row[4]); } catch (...) {}
    if (row.size() > 5) try { n.frequency  = std::stoi(row[5]); } catch (...) {}
    if (row.size() > 6) try { n.last_used  = std::stoll(row[6]); } catch (...) {}
    if (row.size() > 7) try { n.created_at = std::stoll(row[7]); } catch (...) {}
    if (row.size() > 8) try { n.expires_at = row[8].empty() ? 0 : std::stoll(row[8]); } catch (...) {}
    if (row.size() > 9) try { n.deleted_at = row[9].empty() ? 0 : std::stoll(row[9]); } catch (...) {}
    return n;
}

// ---- MemoryStore ----

MemoryStore::MemoryStore(core::Db& db) : db_(db) {}

void MemoryStore::syncKeywords(int64_t id, const std::string& keywords) {
    // Clear old keywords
    db_.run("DELETE FROM memory_keywords WHERE memory_id=?", {std::to_string(id)});

    // Parse comma-separated
    std::istringstream ss(keywords);
    std::string kw;
    while (std::getline(ss, kw, ',')) {
        // trim
        while (!kw.empty() && kw.front() == ' ') kw.erase(kw.begin());
        while (!kw.empty() && kw.back()  == ' ') kw.pop_back();
        if (!kw.empty()) {
            db_.run("INSERT OR IGNORE INTO memory_keywords(memory_id, keyword) VALUES(?,?)",
                    {std::to_string(id), kw});
        }
    }
}

double MemoryStore::jaccardSimilarity(const std::string& a, const std::string& b) const {
    // Simple word-set Jaccard
    auto tokenize = [](const std::string& s) {
        std::vector<std::string> tokens;
        std::istringstream ss(s);
        std::string t;
        while (ss >> t) {
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            tokens.push_back(t);
        }
        return tokens;
    };
    auto ta = tokenize(a);
    auto tb = tokenize(b);

    std::sort(ta.begin(), ta.end());
    std::sort(tb.begin(), tb.end());

    std::vector<std::string> inter, uni;
    std::set_intersection(ta.begin(), ta.end(), tb.begin(), tb.end(), std::back_inserter(inter));
    std::set_union     (ta.begin(), ta.end(), tb.begin(), tb.end(), std::back_inserter(uni));

    if (uni.empty()) return 0.0;
    return static_cast<double>(inter.size()) / static_cast<double>(uni.size());
}

std::vector<MemoryNode> MemoryStore::findSimilar(const std::string& topic,
                                                   const std::string& content,
                                                   double threshold) const {
    std::vector<MemoryNode> result;
    auto nodes = all();
    std::string combined = topic + " " + content;
    for (auto& n : nodes) {
        std::string nc = n.topic + " " + n.content;
        if (jaccardSimilarity(combined, nc) >= threshold) {
            result.push_back(n);
        }
    }
    return result;
}

int64_t MemoryStore::store(const MemoryNode& node, bool force) {
    // Fire PRE_STORE hook
    core::HookContext ctx;
    ctx.set<std::string>("topic",   node.topic);
    ctx.set<std::string>("content", node.content);
    ctx.set<std::string>("keywords", node.keywords);
    core::HookBus::instance().emit(core::HookEvent::PRE_STORE, ctx);
    if (ctx.cancelled) return -1;

    // Hooks may modify topic/content/keywords
    MemoryNode effective = node;
    effective.topic    = ctx.get<std::string>("topic",    node.topic);
    effective.content  = ctx.get<std::string>("content",  node.content);
    effective.keywords = ctx.get<std::string>("keywords", node.keywords);

    // Deduplication check
    if (!force) {
        auto similar = findSimilar(effective.topic, effective.content, 0.85);
        if (!similar.empty()) {
            DuplicateError err("Similar to existing node #" +
                               std::to_string(similar[0].id) +
                               " [" + similar[0].topic + "]. Use --force to store anyway.");
            err.existing_id = similar[0].id;
            throw err;
        }
    }

    int64_t now = nowEpoch();
    std::string expires = effective.expires_at > 0 ? std::to_string(effective.expires_at) : "";

    db_.run(
        "INSERT INTO memory_nodes(topic,content,keywords,importance,frequency,"
        "last_used,created_at,expires_at) VALUES(?,?,?,?,?,?,?,?)",
        {effective.topic, effective.content, effective.keywords,
         std::to_string(effective.importance),
         std::to_string(effective.frequency),
         std::to_string(now), std::to_string(now),
         expires});

    int64_t id = db_.lastInsertId();
    if (!effective.keywords.empty()) syncKeywords(id, effective.keywords);

    // Fire POST_STORE hook
    ctx.set<int64_t>("id", id);
    core::HookBus::instance().emit(core::HookEvent::POST_STORE, ctx);

    return id;
}

bool MemoryStore::update(int64_t id, const std::string& content, const std::string& keywords) {
    int64_t now = nowEpoch();
    db_.run("UPDATE memory_nodes SET content=?, keywords=?, last_used=? WHERE id=?",
            {content, keywords, std::to_string(now), std::to_string(id)});
    if (!keywords.empty()) syncKeywords(id, keywords);

    core::HookContext ctx;
    ctx.set<int64_t>("id", id);
    core::HookBus::instance().emit(core::HookEvent::POST_STORE, ctx);
    return true;
}

bool MemoryStore::remove(int64_t id) {
    int64_t now = nowEpoch();
    db_.run("UPDATE memory_nodes SET deleted_at=? WHERE id=? AND deleted_at IS NULL",
            {std::to_string(now), std::to_string(id)});
    return true;
}

bool MemoryStore::restore(int64_t id) {
    db_.run("UPDATE memory_nodes SET deleted_at=NULL WHERE id=?",
            {std::to_string(id)});
    return true;
}

int MemoryStore::purge(int days_old) {
    int64_t cutoff = nowEpoch() - (int64_t)days_old * 86400;
    int count = 0;
    db_.query("SELECT COUNT(*) FROM memory_nodes WHERE deleted_at IS NOT NULL AND deleted_at < ?",
              {std::to_string(cutoff)},
              [&](const core::Row& r) { if (!r.empty()) try { count = std::stoi(r[0]); } catch (...) {} });
    db_.run("DELETE FROM memory_nodes WHERE deleted_at IS NOT NULL AND deleted_at < ?",
            {std::to_string(cutoff)});
    return count;
}

MemoryNode MemoryStore::get(int64_t id) const {
    MemoryNode node;
    db_.query("SELECT id,topic,content,keywords,importance,frequency,"
              "last_used,created_at,expires_at,deleted_at "
              "FROM memory_nodes WHERE id=?",
              {std::to_string(id)},
              [&](const core::Row& r) { node = rowToNode(r); });
    return node;
}

std::vector<MemoryNode> MemoryStore::all() const {
    std::vector<MemoryNode> result;
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    db_.query("SELECT id,topic,content,keywords,importance,frequency,"
              "last_used,created_at,expires_at,deleted_at "
              "FROM memory_nodes "
              "WHERE deleted_at IS NULL "
              "AND (expires_at IS NULL OR expires_at=0 OR expires_at > ?)",
              {std::to_string(now)},
              [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    return result;
}

std::vector<MemoryNode> MemoryStore::recall(const std::string& query,
                                              int limit, bool /*fuzzy*/) {
    // Fire PRE_RECALL hook
    core::HookContext ctx;
    ctx.set<std::string>("query", query);
    core::HookBus::instance().emit(core::HookEvent::PRE_RECALL, ctx);

    std::string effective_query = ctx.get<std::string>("query", query);

    auto corpus = all();
    auto& scorer = Scorer::instance();
    scorer.fit(corpus);
    auto ranked = scorer.rank(effective_query, corpus, limit);

    // Bump frequency for top result
    if (!ranked.empty()) bumpFrequency(ranked[0].id);

    // Log query
    logQuery(effective_query, (int)ranked.size());

    // Fire POST_RECALL
    ctx.set<int>("result_count", (int)ranked.size());
    core::HookBus::instance().emit(core::HookEvent::POST_RECALL, ctx);

    return ranked;
}

std::vector<MemoryNode> MemoryStore::recallByTopic(const std::string& topic, int limit) {
    std::vector<MemoryNode> result;
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    db_.query("SELECT id,topic,content,keywords,importance,frequency,"
              "last_used,created_at,expires_at,deleted_at "
              "FROM memory_nodes "
              "WHERE topic LIKE ? AND deleted_at IS NULL "
              "AND (expires_at IS NULL OR expires_at=0 OR expires_at > ?) "
              "ORDER BY last_used DESC LIMIT ?",
              {topic + "%", std::to_string(now), std::to_string(limit)},
              [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    return result;
}

void MemoryStore::bumpFrequency(int64_t id) {
    int64_t now = nowEpoch();
    db_.run("UPDATE memory_nodes SET frequency=frequency+1, last_used=? WHERE id=?",
            {std::to_string(now), std::to_string(id)});
}

void MemoryStore::logQuery(const std::string& query, int result_count) {
    db_.run("INSERT INTO query_history(query, matched_ids) VALUES(?,?)",
            {query, std::to_string(result_count)});
}

std::vector<std::string> MemoryStore::queryHistory(int limit) const {
    std::vector<std::string> result;
    db_.query("SELECT query FROM query_history ORDER BY created_at DESC LIMIT ?",
              {std::to_string(limit)},
              [&](const core::Row& r) { if (!r.empty()) result.push_back(r[0]); });
    return result;
}

} // namespace icmg::icm
