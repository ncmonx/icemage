// 2026-10-29: benchmark runner -- see bench_dataset_run.hpp.
#include "bench_dataset_run.hpp"

#include "bench_embed_pool.hpp"
#include "bench_pipeline.hpp"
#include "bench_rerank_pool.hpp"
#include "memory_store.hpp"
#include "../core/db.hpp"
#include "../core/migrator.hpp"
#include "../embed/embedder.hpp"
#include "../embed/embed_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace icmg::imem {

namespace {

constexpr const char* kBenchTopic = "bench";
constexpr const char* kBenchZone  = "bench";
constexpr const char* kBenchSha   = "bench";   // non-empty: skip .git provenance walk

// Set an env var for the duration of a run, restore the caller's value after.
struct ScopedEnv {
    std::string name, prev; bool had = false;
    ScopedEnv(const char* n, const char* v) : name(n) {
        if (const char* p = std::getenv(n)) { prev = p; had = true; }
        set(v);
    }
    ~ScopedEnv() { set(had ? prev : std::string()); }
    void set(const std::string& v) const {
#ifdef _WIN32
        _putenv_s(name.c_str(), v.c_str());
#else
        if (v.empty()) unsetenv(name.c_str()); else setenv(name.c_str(), v.c_str(), 1);
#endif
    }
};

// Throwaway-brain isolation:
//  - ICMG_ATOMIZE=0: async atomization is pointless here.
//  - ICMG_NO_DAEMON=1: every store() flushes the recall cache, which is a pipe
//    RPC to the shared daemon (plus spawn+poll when it is down). Per-doc that
//    made a 1-corpus LoCoMo run 5m26s instead of 5.5s (2026-10-30), and it
//    also flushed the USER's live daemon cache -- a side effect on real state.
struct BenchIsolation {
    ScopedEnv atomize{"ICMG_ATOMIZE", "0"};
    ScopedEnv noDaemon{"ICMG_NO_DAEMON", "1"};
};

}  // namespace

namespace {

void scoreRanked(BenchQResult& r, const BenchQuestion& q, const std::vector<int>& ks) {
    for (int k : ks) {
        r.recall_any[k] = recallAnyAtK(r.ranked, q.evidence, k);
        r.recall_all[k] = recallAllAtK(r.ranked, q.evidence, k);
        r.ndcg[k]       = ndcgAtK(r.ranked, q.evidence, k);
    }
}

std::vector<BenchQResult> runCorpusImpl(const BenchCorpus& corpus,
                                        const std::vector<int>& ks,
                                        const BenchRunOptions& opt,
                                        int* embeddedOut) {
    if (opt.noAsOf) {
        // as_of = 0 -> both recall paths fall back to "everything visible".
        BenchCorpus open = corpus;
        for (auto& q : open.questions) q.as_of = 0;
        BenchRunOptions o = opt;
        o.noAsOf = false;
        return runCorpusImpl(open, ks, o, embeddedOut);
    }
    BenchIsolation isolation;
    core::Db db(":memory:");
    core::Migrator migrator("__nonexistent_migrations_dir__");  // embedded schema
    migrator.runAll(db);
    MemoryStore store(db);
    const bool pipe = opt.pipeline();
    embed::Embedder* embedder = (opt.semantic && !pipe) ? embed::cachedEmbedder() : nullptr;
    int embedded = 0;
    // Pipeline mode stores the enriched text (lexical sees it too) and does
    // its own dense retrieval, so the legacy per-doc embed below is skipped.
    const std::vector<BenchDoc> docs =
        pipe ? enrichBenchDocs(corpus.docs, opt.enrich, opt.neighbours) : corpus.docs;
    std::vector<int64_t> nodeIds(docs.size(), 0);

    // Undated docs must stay visible to every question (valid_from=0 would
    // default to "now" and hide them behind any historical as_of).
    std::map<int64_t, std::string> nodeToDoc;
    db.run("BEGIN");
    for (size_t di = 0; di < docs.size(); ++di) {
        const auto& d = docs[di];
        if (d.text.empty()) continue;
        MemoryNode n;
        n.topic = kBenchTopic;
        n.zone = kBenchZone;
        n.git_sha = kBenchSha;
        n.source = "bench";
        n.content = d.text;
        n.valid_from = d.date > 0 ? d.date : 1;
        int64_t id = store.store(n, /*force=*/true);
        if (id > 0) {
            nodeIds[di] = id;
            nodeToDoc[id] = d.id;
            if (embedder) {
                // Same text the live path embeds (topic + content) so the
                // benchmark measures the production blend, not a variant.
                auto v = embedder->embed(n.topic + " " + n.content);
                if (!v.empty()) {
                    embed::EmbedStore(db).put(id, "memory", v, embedder->model(), "");
                    ++embedded;
                }
            }
        }
    }
    db.run("COMMIT");

    const int maxK = ks.empty() ? 10 : *std::max_element(ks.begin(), ks.end());
    std::vector<std::vector<std::string>> piped;
    if (pipe)
        piped = runBenchPipeline(docs, nodeIds, corpus.questions, store, opt, maxK, &embedded);
    std::vector<BenchQResult> out;
    out.reserve(corpus.questions.size());
    for (size_t qi = 0; qi < corpus.questions.size(); ++qi) {
        const auto& q = corpus.questions[qi];
        BenchQResult r;
        r.id = q.id;
        r.category = q.category;
        if ((q.abstention && !opt.scoreAbstention) || q.evidence.empty()) {
            r.skipped = true;
            out.push_back(std::move(r));
            continue;
        }
        if (pipe) {
            r.ranked = piped[qi];
            scoreRanked(r, q, ks);
            out.push_back(std::move(r));
            continue;
        }
        int64_t asOf = q.as_of > 0 ? q.as_of : std::numeric_limits<int32_t>::max();
        auto hits = embedder ? store.recallSemanticAsOf(q.question, asOf, maxK, opt.alpha)
                             : store.recallAsOf(q.question, asOf, maxK);
        for (const auto& node : hits) {
            auto it = nodeToDoc.find(node.id);
            if (it != nodeToDoc.end() &&
                std::find(r.ranked.begin(), r.ranked.end(), it->second) == r.ranked.end())
                r.ranked.push_back(it->second);
        }
        scoreRanked(r, q, ks);
        out.push_back(std::move(r));
    }
    if (embeddedOut) *embeddedOut = embedded;
    return out;
}

}  // namespace

std::string describePipeline(const BenchRunOptions& o) {
    char b[256];
    const bool dense = o.semantic && BenchEmbedPool::instance().available();
    std::string s = dense ? "pipeline dense" : (o.semantic ? "pipeline semantic-unavailable" : "pipeline bm25");
    if (dense) {
        if (o.fusion == "rrf") s += " fusion=rrf";
        else { std::snprintf(b, sizeof b, " fusion=blend alpha=%.2f", o.alpha); s += b; }
    }
    if (o.prfBeta > 0) { std::snprintf(b, sizeof b, " prf=%.2f", o.prfBeta); s += b; }
    if (o.enrich) s += " enrich";
    if (o.neighbours > 0) s += " nbr=" + std::to_string(o.neighbours);
    if (o.chunkWords > 0) s += " chunk=" + std::to_string(o.chunkWords);
    if (o.userView) s += " user-view";
    if (o.temporal > 0) { std::snprintf(b, sizeof b, " temporal=%.2f", o.temporal); s += b; }
    if (o.groupBeta > 0) { std::snprintf(b, sizeof b, " group=%.2f", o.groupBeta); s += b; }
    if (o.coverLambda > 0) { std::snprintf(b, sizeof b, " cover=%.2f", o.coverLambda); s += b; }
    if (o.adjacent > 0) s += " adj=" + std::to_string(o.adjacent) + "x" + std::to_string(o.adjacentSeeds);
    if (o.mmrLambda < 1.0) { std::snprintf(b, sizeof b, " mmr=%.2f", o.mmrLambda); s += b; }
    if (o.rerankTop > 0)
        s += " rerank=" + std::to_string(o.rerankTop) +
             (BenchRerankPool::instance().available() ? "" : "(unavailable)");
    if (o.rerankTop > 0 && o.rerankChunk > 0)
        s += " rerank-chunk=" + std::to_string(o.rerankChunk);
    if (o.rerankGate > 0) { std::snprintf(b, sizeof b, " gate=%.2f", o.rerankGate); s += b; }
    return s;
}

std::vector<BenchQResult> runBenchCorpus(const BenchCorpus& corpus,
                                         const std::vector<int>& ks,
                                         const BenchRunOptions& opt) {
    return runCorpusImpl(corpus, ks, opt, nullptr);
}

BenchFileReport runBenchDatasetFile(const std::string& dataset,
                                    const std::string& path,
                                    BenchGranularity g,
                                    const std::vector<int>& ks,
                                    int maxCorpora,
                                    const BenchRunOptions& opt) {
    if (dataset != "longmemeval" && dataset != "locomo")
        throw std::runtime_error("unknown dataset '" + dataset +
                                 "' (expected longmemeval | locomo)");
    resetBenchRerankGateStats();
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read dataset file: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();

    BenchFileReport rep;
    rep.dataset = dataset;
    if (opt.semantic) {
        char a[32]; std::snprintf(a, sizeof a, "semantic alpha=%.2f", opt.alpha);
        rep.mode = embed::cachedEmbedder() ? std::string(a)
                                           : std::string("semantic-unavailable (bm25 fallback)");
    }
    if (opt.pipeline()) rep.mode = describePipeline(opt);
    if (opt.noAsOf) rep.mode += " no-as-of";
    if (opt.scoreAbstention) rep.mode += " +abstention";
    auto t0 = std::chrono::steady_clock::now();
    // Counting pass: a cheap brace scan, so progress lines can show a total.
    int total = forEachTopLevelObject(text, [](const std::string&) {});
    if (maxCorpora > 0 && maxCorpora < total) total = maxCorpora;
    double lastReport = 0.0;
    forEachTopLevelObject(text, [&](const std::string& obj) {
        if (maxCorpora > 0 && rep.corpora >= maxCorpora) return;
        BenchCorpus c = dataset == "longmemeval" ? parseLongMemEval(obj, g)
                                                 : parseLoCoMo(obj, g);
        rep.docs += (int)c.docs.size();
        int emb = 0;
        auto rs = runCorpusImpl(c, ks, opt, &emb);
        rep.embedded += emb;
        rep.results.insert(rep.results.end(), rs.begin(), rs.end());
        ++rep.corpora;
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (el - lastReport >= kBenchProgressEverySec || rep.corpora == total) {
            std::cerr << formatBenchProgress(rep.corpora, total, el) << std::endl;
            lastReport = el;
        }
    });
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (opt.rerankGate > 0) {
        const auto g = benchRerankGateStats();
        std::cerr << "[bench] rerank gate skipped " << g.skipped << "/" << g.considered
                  << " questions" << std::endl;
    }
    return rep;
}

std::string formatBenchProgress(int done, int total, double elapsedSec) {
    char buf[160];
    int pct = total > 0 ? (int)(100.0 * done / total) : 0;
    int n = std::snprintf(buf, sizeof buf, "[bench] corpus %d/%d (%d%%) %.0fs elapsed",
                          done, total, pct, elapsedSec);
    std::string s(buf, n > 0 ? (size_t)n : 0);
    if (done > 0 && done < total) {
        double left = elapsedSec / done * (total - done);
        std::snprintf(buf, sizeof buf, " ~%.0fs left", left);
        s += buf;
    }
    return s;
}

std::vector<int> parseBenchKs(const std::string& csv) {
    std::vector<int> ks;
    std::stringstream ss(csv);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try {
            int k = std::stoi(tok);
            if (k > 0 && std::find(ks.begin(), ks.end(), k) == ks.end()) ks.push_back(k);
        } catch (...) {}
    }
    if (ks.empty()) ks = {1, 5, 10};
    std::sort(ks.begin(), ks.end());
    return ks;
}

std::string formatBenchReport(const BenchFileReport& rep, const std::vector<int>& ks,
                              bool asJson, bool perQuestion) {
    BenchAgg agg = aggregateBench(rep.results, ks);
    auto pct = [](double v) {
        char b[16]; std::snprintf(b, sizeof b, "%5.1f", v * 100.0); return std::string(b);
    };
    if (asJson) {
        nlohmann::json j;
        j["dataset"] = rep.dataset;
        j["mode"] = rep.mode;
        j["corpora"] = rep.corpora;
        j["docs"] = rep.docs;
        j["embedded"] = rep.embedded;
        j["seconds"] = rep.seconds;
        j["scored"] = agg.scored;
        j["skipped"] = agg.skipped;
        auto catJson = [&](const BenchCatAgg& c) {
            nlohmann::json o;
            o["n"] = c.n;
            for (int k : ks) {
                const std::string s = std::to_string(k);
                o["recall_any@" + s] = c.recall_any.count(k) ? c.recall_any.at(k) : 0.0;
                o["recall_all@" + s] = c.recall_all.count(k) ? c.recall_all.at(k) : 0.0;
                o["ndcg@" + s]       = c.ndcg.count(k) ? c.ndcg.at(k) : 0.0;
            }
            return o;
        };
        j["overall"] = catJson(agg.overall);
        for (const auto& [name, c] : agg.by_category) j["by_category"][name] = catJson(c);
        if (perQuestion) {
            j["questions"] = nlohmann::json::array();
            for (const auto& r : rep.results)
                j["questions"].push_back({{"id", r.id}, {"category", r.category},
                                          {"skipped", r.skipped}, {"ranked", r.ranked}});
        }
        return j.dump(2) + "\n";
    }

    std::ostringstream o;
    o << "=== bench-recall --dataset " << rep.dataset << " (retrieval-only, " << rep.mode
      << ", as-of) ===\n"
      << "  corpora: " << rep.corpora << "   docs stored: " << rep.docs;
    if (rep.embedded > 0) o << "   embedded: " << rep.embedded;
    o << "   questions scored: " << agg.scored << "   skipped (abstention/no evidence): "
      << agg.skipped << "   time: ";
    char t[32]; std::snprintf(t, sizeof t, "%.1fs", rep.seconds);
    o << t << "\n\n";
    o << "  category                          n";
    for (int k : ks) o << "   R_any@" << k << "  R_all@" << k << "  NDCG@" << k;
    o << "\n";
    auto row = [&](const std::string& name, const BenchCatAgg& c) {
        char nb[48]; std::snprintf(nb, sizeof nb, "  %-30s %4d", name.c_str(), c.n);
        o << nb;
        for (int k : ks) {
            o << "   " << pct(c.recall_any.count(k) ? c.recall_any.at(k) : 0.0)
              << "   " << pct(c.recall_all.count(k) ? c.recall_all.at(k) : 0.0)
              << "  " << pct(c.ndcg.count(k) ? c.ndcg.at(k) : 0.0);
        }
        o << "\n";
    };
    for (const auto& [name, c] : agg.by_category) row(name, c);
    row("OVERALL", agg.overall);
    o << "\n  (percent; abstention questions excluded per LongMemEval convention.\n"
         "   Retrieval-only: QA accuracy needs an LLM reader + judge, not run here.)\n";
    return o.str();
}

}  // namespace icmg::imem
