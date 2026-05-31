#include "memory_store.hpp"
#include "atom_store.hpp"
#include <ctime>
#include <cstdlib>
#include "../core/recall_cache.hpp"   // ram-brain: hot recall cache
#include "../core/recall_cache_client.hpp"
#include "../cli/recall_json.hpp"
#include <cstdlib>
#include "scorer.hpp"
#include "../core/hook_bus.hpp"
#include "../core/user_identity.hpp"
#include "../core/exec_utils.hpp"
#include <unordered_set>
#include "../embed/embedder.hpp"
#include "../embed/embed_store.hpp"
#include <chrono>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <unordered_map>

namespace icmg::imem {

// ram-brain: process-local hot recall cache + global-flush epoch.
core::RecallCache& MemoryStore::recallCache() { static core::RecallCache c; return c; }
std::int64_t& MemoryStore::recallEpoch() { static std::int64_t e = 0; return e; }
namespace {
bool rcEnabled() { const char* v = std::getenv("ICMG_RECALL_CACHE"); return !(v && v[0] == '0'); }
std::string rcKey(const std::string& q, int limit, const std::string& scope, std::int64_t epoch) {
    return std::to_string(epoch) + "|" + scope + "|" + std::to_string(limit) + "|" + q;
}
void rcFlushOnWrite() { ++MemoryStore::recallEpoch(); MemoryStore::recallCache().flush(); core::rcacheDaemonFlush(); }
} // anon

// ---- helpers ----

static std::string captureGitSha() {
    auto res = core::safeExecShell("git rev-parse --short HEAD 2>/dev/null", false, 3000);
    if (res.exit_code != 0 || res.out.empty()) return "";
    std::string sha = res.out;
    while (!sha.empty() && (sha.back() == '\n' || sha.back() == '\r' || sha.back() == ' '))
        sha.pop_back();
    return sha;
}

int64_t MemoryStore::nowEpoch() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// v1.1.0 Task 3: bulk-load every memory_node embedding into the in-memory
// cache on first semantic-recall call. Subsequent recalls within the same
// MemoryStore lifetime hit the cache directly — zero EmbedStore DB calls.
// Cleared on store() to stay coherent.
void MemoryStore::warmEmbedCache(int dim) const {
    if (embed_cache_warmed_) return;
    embed_cache_warmed_ = true;
    try {
        std::vector<int64_t> all_ids;
        db_.query("SELECT id FROM memory_nodes", {},
                  [&](const core::Row& r) {
                      if (r.empty()) return;
                      try { all_ids.push_back(std::stoll(r[0])); } catch (...) {}
                  });
        if (all_ids.empty()) return;
        embed::EmbedStore es(db_);
        auto vecs = es.getMany("memory", all_ids, dim);
        for (auto& kv : vecs) {
            embed_cache_[kv.first] = std::move(kv.second);
        }
    } catch (...) {
        // Cache stays empty; recall falls back to miss_ids path (DB query).
    }
}

MemoryNode MemoryStore::rowToNode(const core::Row& row) const {
    // Columns: id, topic, content, keywords, importance, frequency,
    //          last_used, created_at, expires_at, deleted_at, zone, pinned, git_sha
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
    if (row.size() > 10 && !row[10].empty()) n.zone = row[10];
    if (row.size() > 11) try { n.pinned  = std::stoi(row[11]); } catch (...) {}
    if (row.size() > 12 && !row[12].empty()) n.git_sha = row[12];
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

    // v1.62 F15: optional semantic near-dup pass. Word-set Jaccard misses
    // cross-phrasing ("fixed X bug" vs "X bug resolved"). When
    // ICMG_SEMANTIC_DEDUP=1 and an embedder is available, also flag nodes
    // whose embedding cosine to the query >= 0.95 as duplicates. Opt-in so
    // the default store() hot path stays embedding-free; Jaccard remains the
    // primary signal and fallback.
    if (const char* envsd = std::getenv("ICMG_SEMANTIC_DEDUP");
        envsd && *envsd && std::string(envsd) != "0") {
        if (auto* embedder = embed::cachedEmbedder()) {
            auto qvec = embedder->embed(combined);
            int dim = embedder->dim();
            if (!qvec.empty() && !nodes.empty()) {
                std::vector<int64_t> ids;
                ids.reserve(nodes.size());
                for (auto& n : nodes) ids.push_back(n.id);
                embed::EmbedStore es(db_);
                auto vecs = es.getMany("memory", ids, dim);
                std::unordered_map<int64_t, std::vector<float>> byid;
                for (auto& kv : vecs) byid[kv.first] = kv.second;
                std::unordered_set<int64_t> seen;
                for (auto& r : result) seen.insert(r.id);
                for (auto& n : nodes) {
                    if (seen.count(n.id)) continue;
                    auto it = byid.find(n.id);
                    if (it == byid.end() || it->second.empty()) continue;
                    if (embed::cosine(qvec, it->second) >= 0.95f) {
                        result.push_back(n);
                        seen.insert(n.id);
                    }
                }
            }
        }
    }
    return result;
}

int64_t MemoryStore::store(const MemoryNode& node, bool force) {
    rcFlushOnWrite();   // ram-brain: invalidate recall cache on any write
    // v1.1.0 Task 3: any write invalidates the embed cache. Next recall
    // refreshes from DB. Cheap (clear map + bool flip).
    embed_cache_.clear();
    embed_cache_warmed_ = false;

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
            // v1.21.0 (M3): silent upsert mode — `ICMG_DEDUP_SILENT=1` env
            // makes store() return the existing id + bump frequency instead
            // of throwing. Used by 3-layer auto-extract hooks where dup
            // throws would spam stderr. Default behavior (throw) preserved
            // for interactive `icmg store` so users still see warnings.
            const char* silent = std::getenv("ICMG_DEDUP_SILENT");
            if (silent && *silent && std::string(silent) != "0") {
                bumpFrequency(similar[0].id);
                return similar[0].id;
            }
            DuplicateError err("Similar to existing node #" +
                               std::to_string(similar[0].id) +
                               " [" + similar[0].topic + "]. Use --force to store anyway.");
            err.existing_id = similar[0].id;
            throw err;
        }
    }

    int64_t now = nowEpoch();
    std::string expires = effective.expires_at > 0 ? std::to_string(effective.expires_at) : "";

    std::string zone = effective.zone.empty() ? "default" : effective.zone;
    std::string git_sha = effective.git_sha.empty() ? captureGitSha() : effective.git_sha;
    // Phase 47 T4: tag created_by from user_identity (env / git config / anonymous).
    db_.run(
        "INSERT INTO memory_nodes(topic,content,keywords,importance,frequency,"
        "last_used,created_at,expires_at,zone,created_by,git_sha) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
        {effective.topic, effective.content, effective.keywords,
         std::to_string(effective.importance),
         std::to_string(effective.frequency),
         std::to_string(now), std::to_string(now),
         expires, zone, core::currentUser(), git_sha});
    // Phase 48: initial row_version=1 for sync tracking.
    int64_t new_id = db_.lastInsertId();
    try { db_.run("UPDATE memory_nodes SET row_version=1 WHERE id=?", {std::to_string(new_id)}); } catch(...) {}

    int64_t id = db_.lastInsertId();
    if (!effective.keywords.empty()) syncKeywords(id, effective.keywords);

    // Fire POST_STORE hook
    ctx.set<int64_t>("id", id);
    core::HookBus::instance().emit(core::HookEvent::POST_STORE, ctx);

    // v1.79 ICM dual-memory: enqueue for async atomization (best-effort, never
    // throws on hot path). Opt-out ICMG_ATOMIZE=0. Atoms appear after a worker
    // run (icmg atomize run / background tick), NOT synchronously.
    {
        const char* off = std::getenv("ICMG_ATOMIZE");
        if (!(off && off[0] == '0')) {
            try { AtomStore(db_).enqueue(id, std::time(nullptr)); } catch (...) {}
        }
    }

    return id;
}

bool MemoryStore::update(int64_t id, const std::string& content, const std::string& keywords) {
    int64_t now = nowEpoch();
    db_.run("UPDATE memory_nodes SET content=?, keywords=?, last_used=?, "
            "row_version=COALESCE(row_version,0)+1 WHERE id=?",
            {content, keywords, std::to_string(now), std::to_string(id)});
    if (!keywords.empty()) syncKeywords(id, keywords);

    core::HookContext ctx;
    ctx.set<int64_t>("id", id);
    core::HookBus::instance().emit(core::HookEvent::POST_STORE, ctx);
    return true;
}

bool MemoryStore::remove(int64_t id) {
    rcFlushOnWrite();   // ram-brain: invalidate recall cache on any write
    int64_t now = nowEpoch();
    db_.run("UPDATE memory_nodes SET deleted_at=? WHERE id=? AND deleted_at IS NULL",
            {std::to_string(now), std::to_string(id)});
    return true;
}

bool MemoryStore::restore(int64_t id) {
    rcFlushOnWrite();   // ram-brain: invalidate recall cache on any write
    db_.run("UPDATE memory_nodes SET deleted_at=NULL WHERE id=?",
            {std::to_string(id)});
    return true;
}

int MemoryStore::purge(int days_old) {
    rcFlushOnWrite();   // ram-brain: invalidate recall cache on any write
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
              "last_used,created_at,expires_at,deleted_at,zone,pinned "
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
              "last_used,created_at,expires_at,deleted_at,zone,pinned "
              "FROM memory_nodes "
              "WHERE deleted_at IS NULL "
              "AND (expires_at IS NULL OR expires_at=0 OR expires_at > ?)",
              {std::to_string(now)},
              [&](const core::Row& r) { result.push_back(rowToNode(r)); });

    // Phase 27 T3: pull avg feedback per node (last 30d) -> feedback_bias.
    // Bias formula: 1 + 0.2 * ((avg-2.5)/2.5), clamped [0.8, 1.2].
    // No-op if feedback table missing (try/catch).
    try {
        int64_t cutoff = now - 30LL * 86400;
        std::unordered_map<int64_t, double> avg;
        db_.query("SELECT node_id, AVG(score) FROM feedback "
                  "WHERE created_at > ? GROUP BY node_id",
                  {std::to_string(cutoff)},
                  [&](const core::Row& r){
                      if (r.size() < 2) return;
                      try { avg[std::stoll(r[0])] = std::stod(r[1]); } catch (...) {}
                  });
        for (auto& n : result) {
            auto it = avg.find(n.id);
            if (it == avg.end()) continue;
            double mean = it->second;
            double bias = 1.0 + 0.2 * ((mean - 2.5) / 2.5);
            if (bias < 0.8) bias = 0.8;
            if (bias > 1.2) bias = 1.2;
            n.feedback_bias = bias;
        }
    } catch (...) {}
    return result;
}

std::vector<MemoryNode> MemoryStore::recall(const std::string& query,
                                              int limit, bool /*fuzzy*/) {
    // Fire PRE_RECALL hook
    core::HookContext ctx;
    ctx.set<std::string>("query", query);
    core::HookBus::instance().emit(core::HookEvent::PRE_RECALL, ctx);

    std::string effective_query = ctx.get<std::string>("query", query);

    std::string _rckey;
    if (rcEnabled()) {
        _rckey = rcKey(effective_query, limit, "default", recallEpoch());
        if (auto _hit = recallCache().get(_rckey))
            return icmg::cli::cacheNodesFromJson(*_hit);
        if (auto _dh = core::rcacheDaemonGet(_rckey)) {   // daemon-shared tier
            recallCache().put(_rckey, *_dh);
            return icmg::cli::cacheNodesFromJson(*_dh);
        }
    }

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

    if (!_rckey.empty()) {
        auto _js = icmg::cli::cacheNodesToJson(ranked);
        recallCache().put(_rckey, _js);
        core::rcacheDaemonPut(_rckey, _js);   // share to daemon (best-effort)
    }
    return ranked;
}

// v1.1.0 Task 4: diff-aware recall. Filters out nodes whose
// last_returned_session already matches the current session_id, then stamps
// the returned set so a re-recall in the same session returns less.
std::vector<MemoryNode> MemoryStore::recallUnseen(const std::string& query,
                                                    const std::string& session_id,
                                                    int limit, bool fuzzy) {
    auto candidates = recall(query, limit * 4, fuzzy); // over-fetch then filter
    if (session_id.empty() || candidates.empty()) {
        if ((int)candidates.size() > limit) candidates.resize(limit);
        return candidates;
    }

    // Read last_returned_session per candidate. Single SELECT IN (...) query.
    std::ostringstream ids_sql;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i) ids_sql << ",";
        ids_sql << candidates[i].id;
    }
    std::unordered_map<int64_t, std::string> seen;
    try {
        db_.query(
            "SELECT id, COALESCE(last_returned_session,'') FROM memory_nodes "
            "WHERE id IN (" + ids_sql.str() + ")",
            {},
            [&](const core::Row& r) {
                if (r.size() < 2) return;
                try { seen[std::stoll(r[0])] = r[1]; } catch (...) {}
            });
    } catch (...) {}

    std::vector<MemoryNode> out;
    out.reserve(limit);
    std::vector<int64_t> to_stamp;
    for (auto& n : candidates) {
        auto it = seen.find(n.id);
        if (it != seen.end() && it->second == session_id) continue;
        out.push_back(n);
        to_stamp.push_back(n.id);
        if ((int)out.size() >= limit) break;
    }

    // Stamp the returned set as seen this session.
    if (!to_stamp.empty()) {
        std::ostringstream upd;
        upd << "UPDATE memory_nodes SET last_returned_session=? WHERE id IN (";
        for (size_t i = 0; i < to_stamp.size(); ++i) {
            if (i) upd << ",";
            upd << to_stamp[i];
        }
        upd << ")";
        try { db_.run(upd.str(), {session_id}); } catch (...) {}
    }
    return out;
}

std::vector<MemoryNode> MemoryStore::recallInZone(const std::string& query,
                                                    const std::string& zone,
                                                    int limit, bool /*fuzzy*/) {
    // Filter corpus to the requested zone before fit/rank — sharper IDF + faster.
    auto full = all();
    std::vector<MemoryNode> corpus;
    corpus.reserve(full.size());
    for (auto& n : full) {
        if (n.zone == zone) corpus.push_back(std::move(n));
    }
    auto& scorer = Scorer::instance();
    scorer.invalidate();   // force re-fit on new (smaller) corpus
    scorer.fit(corpus);
    auto ranked = scorer.rank(query, corpus, limit);
    if (!ranked.empty()) bumpFrequency(ranked[0].id);
    logQuery(query + " [zone=" + zone + "]", (int)ranked.size());
    // Restore global IDF for subsequent unscoped recalls
    scorer.invalidate();
    return ranked;
}

std::vector<MemoryNode> MemoryStore::recallByTopic(const std::string& topic, int limit) {
    std::vector<MemoryNode> result;
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    db_.query("SELECT id,topic,content,keywords,importance,frequency,"
              "last_used,created_at,expires_at,deleted_at,zone,pinned "
              "FROM memory_nodes "
              "WHERE topic LIKE ? AND deleted_at IS NULL "
              "AND (expires_at IS NULL OR expires_at=0 OR expires_at > ?) "
              "ORDER BY last_used DESC LIMIT ?",
              {topic + "%", std::to_string(now), std::to_string(limit)},
              [&](const core::Row& r) { result.push_back(rowToNode(r)); });
    return result;
}

std::vector<MemoryNode> MemoryStore::recallSemantic(const std::string& query,
                                                     int limit, double alpha) {
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;

    // Step 1: BM25 candidate set (top 50, cheap).
    auto corpus = all();
    auto& scorer = Scorer::instance();
    scorer.fit(corpus);
    int cand_n = std::max(limit * 5, 50);
    auto cand = scorer.rank(query, corpus, cand_n);
    if (cand.empty()) return cand;

    // Pure BM25 path (alpha == 1) — skip embed entirely.
    if (alpha >= 0.999) {
        if ((int)cand.size() > limit) cand.resize(limit);
        if (!cand.empty()) bumpFrequency(cand[0].id);
        logQuery(query + " [semantic alpha=1]", (int)cand.size());
        return cand;
    }

    // Step 2: try to embed query.
    // v1.28.0: cached singleton — first call inside this process pays the
    // ONNX cold-load (~5-6s on Win NTFS); subsequent calls reuse the
    // session. Cuts test_vec_search ctest from 31s to <1s by removing
    // 5x redundant cold-loads across 5 sub-tests.
    auto* embedder = embed::cachedEmbedder();
    if (!embedder) {
        // Graceful fallback: return BM25 results unchanged.
        if ((int)cand.size() > limit) cand.resize(limit);
        if (!cand.empty()) bumpFrequency(cand[0].id);
        logQuery(query + " [semantic no-embedder]", (int)cand.size());
        return cand;
    }
    auto qvec = embedder->embed(query);
    int dim = embedder->dim();
    if (qvec.empty()) {
        if ((int)cand.size() > limit) cand.resize(limit);
        return cand;
    }

    // Step 3: load doc embeddings for candidate ids.
    // v1.1.0 Task 3: hot-path cache. Warm once per MemoryStore lifetime;
    // subsequent recalls skip the EmbedStore DB query entirely.
    warmEmbedCache(dim);
    std::unordered_map<int64_t, std::vector<float>> by_id;
    {
        std::vector<int64_t> miss_ids;
        for (auto& n : cand) {
            auto it = embed_cache_.find(n.id);
            if (it != embed_cache_.end()) {
                by_id.emplace(n.id, it->second);
            } else {
                miss_ids.push_back(n.id);
            }
        }
        if (!miss_ids.empty()) {
            embed::EmbedStore es(db_);
            auto vecs = es.getMany("memory", miss_ids, dim);
            for (auto& kv : vecs) {
                embed_cache_[kv.first] = kv.second;
                by_id.emplace(kv.first, std::move(kv.second));
            }
        }
    }

    // Step 4: BM25 normalisation (max-score = 1).
    double max_bm = 0.0;
    for (auto& n : cand) {
        // bm25_score lives in n.frequency? No — Scorer doesn't return raw scores publicly.
        // Use rank-based fallback: position-derived score.
        // (Scorer ranks already; we rebuild a normalized BM25 by inverse rank.)
    }
    // Rank-based BM25 normalization: top doc=1.0 -> bottom near 0.
    int N = (int)cand.size();
    auto bm_norm = [&](int idx) {
        return (N <= 1) ? 1.0 : 1.0 - (double)idx / (double)(N - 1);
    };
    (void)max_bm;

    // Step 5: hybrid blend.
    struct Scored { MemoryNode node; double score; };
    std::vector<Scored> blended;
    blended.reserve(cand.size());
    for (int i = 0; i < N; ++i) {
        double bm = bm_norm(i);
        double cs = 0.0;
        auto it = by_id.find(cand[i].id);
        if (it != by_id.end()) {
            cs = (double)embed::cosine(qvec, it->second);
            if (cs < 0.0) cs = 0.0;
        }
        double s = alpha * bm + (1.0 - alpha) * cs;
        blended.push_back({cand[i], s});
    }
    std::sort(blended.begin(), blended.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    std::vector<MemoryNode> out;
    out.reserve(std::min(limit, (int)blended.size()));
    for (int i = 0; i < (int)blended.size() && i < limit; ++i)
        out.push_back(std::move(blended[i].node));

    if (!out.empty()) bumpFrequency(out[0].id);
    logQuery(query + " [semantic alpha=" + std::to_string(alpha) + "]", (int)out.size());
    return out;
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

} // namespace icmg::imem
