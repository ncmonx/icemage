// 2026-10-31: see bench_pipeline.hpp.
#include "bench_pipeline.hpp"

#include "bench_embed_pool.hpp"
#include <atomic>
#include "bench_expand.hpp"
#include "bench_gate_trace.hpp"
#include "bench_rerank_pool.hpp"
#include "bench_retrieval.hpp"
#include "memory_store.hpp"
#include "scorer.hpp"
#include "../embed/embedder.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <unordered_map>

namespace icmg::imem {

namespace {

constexpr int kPoolMin       = 100;  // candidates per retriever before fusion
constexpr int kPoolPerK      = 10;
constexpr int kMmrPool       = 50;   // MMR re-orders only this head
constexpr int kUserViewWords = 90;   // chunk size when userView has no --chunk
constexpr int kRerankPassages   = 4; // passages per doc scored by --rerank-chunk
constexpr int kRerankOverlapDiv = 4; // passage overlap = chunk / 4

// "sid" of a doc only when it really is a turn inside a session.
bool isTurnId(const std::string& id) { return benchGroupOf(id) != id; }

std::atomic<long long> gGateConsidered{0}, gGateSkipped{0};

// --adjacent: same-session turns around `id` in corpus order, nearest first and
// the following turn before the preceding one (answers follow questions).
// Only stored docs (nodeId > 0); non-turn docs have no neighbours.
std::vector<std::string> sessionNeighbours(const std::string& id, int w,
                                           const std::vector<BenchDoc>& docs,
                                           const std::unordered_map<std::string, size_t>& docIdx,
                                           const std::vector<int64_t>& nodeIds) {
    std::vector<std::string> out;
    auto it = docIdx.find(id);
    if (it == docIdx.end() || !isTurnId(id)) return out;
    const size_t i = it->second;
    const std::string g = benchGroupOf(id);
    auto take = [&](size_t j) {
        if (j < docs.size() && nodeIds[j] > 0 && benchGroupOf(docs[j].id) == g) out.push_back(docs[j].id);
    };
    for (int d = 1; d <= w; ++d) {
        take(i + (size_t)d);
        if (i >= (size_t)d) take(i - (size_t)d);
    }
    return out;
}

// RM3 (--prf): question + expansion terms from the top lexical hits run as a
// second as-of lexical query; per-doc normalized scores interpolated
// (1-beta)*orig + beta*exp. ids/norm are rewritten sorted by the new score.
void applyPrf(const std::string& question, int64_t asOf, int poolN,
              const std::vector<BenchDoc>& docs,
              const std::unordered_map<std::string, size_t>& docIdx,
              const std::unordered_map<int64_t, size_t>& docOfNode,
              MemoryStore& store, double beta,
              std::vector<std::string>& ids, std::vector<double>& norm) {
    const auto& sc = Scorer::instance();
    std::vector<std::vector<std::string>> fbTok;
    std::vector<double> fbScore;
    for (size_t i = 0; i < ids.size() && (int)i < kPrfFeedbackDocs; ++i) {
        auto it = docIdx.find(ids[i]);
        if (it == docIdx.end()) continue;
        fbTok.push_back(sc.tokenize(docs[it->second].text));
        fbScore.push_back(norm[i] > 0 ? norm[i] : 1e-3);   // min-max gives the last hit 0
    }
    auto terms = rm3ExpansionTerms(sc.tokenize(question), fbTok, fbScore, kPrfTerms);
    if (terms.empty()) return;
    // q' keeps the original terms (RM3, not pure RM1): an expansion-only
    // query drifts (LoCoMo any@1 .308 -> .172 at beta .5, 2026-11-11).
    std::string expQ = question;
    for (const auto& t : terms) expQ += " " + t.term;

    std::vector<std::string> eIds;
    std::vector<double> eRaw;
    for (const auto& h : store.recallAsOf(expQ, asOf, poolN)) {
        auto it = docOfNode.find(h.id);
        if (it == docOfNode.end()) continue;
        eIds.push_back(docs[it->second].id);
        eRaw.push_back(h.bm25_score);
    }
    auto eNorm = Scorer::normalizeMinMax(eRaw);
    std::map<std::string, double> s;
    for (size_t i = 0; i < ids.size(); ++i) s[ids[i]] += (1.0 - beta) * norm[i];
    for (size_t i = 0; i < eIds.size(); ++i) s[eIds[i]] += beta * eNorm[i];
    std::vector<std::pair<std::string, double>> v(s.begin(), s.end());
    std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if ((int)v.size() > poolN) v.resize(poolN);
    ids.clear();
    norm.clear();
    for (auto& [id, x] : v) { ids.push_back(id); norm.push_back(x); }
}

}  // namespace

std::vector<BenchDoc> enrichBenchDocs(const std::vector<BenchDoc>& docs,
                                      bool dateLabel, int neighbours) {
    std::vector<BenchDoc> out = docs;
    if (neighbours > 0) {
        for (size_t i = 0; i < docs.size(); ++i) {
            if (!isTurnId(docs[i].id)) continue;
            const std::string g = benchGroupOf(docs[i].id);
            std::string before, after;
            for (int d = neighbours; d >= 1; --d)
                if (i >= (size_t)d && benchGroupOf(docs[i - d].id) == g)
                    before += docs[i - d].text + "\n";
            for (int d = 1; d <= neighbours; ++d)
                if (i + d < docs.size() && benchGroupOf(docs[i + d].id) == g)
                    after += "\n" + docs[i + d].text;
            if (!before.empty() || !after.empty())
                out[i].text = before + docs[i].text + after;
        }
    }
    if (dateLabel)
        for (auto& d : out) {
            auto lbl = benchDateLabel(d.date);
            if (!lbl.empty()) d.text = "[" + lbl + "] " + d.text;
        }
    return out;
}

std::vector<std::vector<std::string>>
runBenchPipeline(const std::vector<BenchDoc>& docs,
                 const std::vector<int64_t>& nodeIds,
                 const std::vector<BenchQuestion>& questions,
                 MemoryStore& store, const BenchRunOptions& opt,
                 int maxK, int* embedded) {
    auto& pool = BenchEmbedPool::instance();
    const bool dense = opt.semantic && pool.available();
    const int poolN = std::max(kPoolMin, maxK * kPoolPerK);

    std::unordered_map<int64_t, size_t> docOfNode;
    for (size_t i = 0; i < nodeIds.size(); ++i)
        if (nodeIds[i] > 0) docOfNode[nodeIds[i]] = i;
    std::unordered_map<std::string, size_t> docIdx;   // evidence id -> docs[] index
    for (size_t i = 0; i < docs.size(); ++i) docIdx[docs[i].id] = i;
    auto& rerank = BenchRerankPool::instance();
    const bool rerankOn = opt.rerankTop > 0 && rerank.available();

    // ---- dense index: every doc -> one or more vectors (max-sim) ----
    std::vector<std::vector<std::vector<float>>> docVecs(docs.size());
    if (dense) {
        std::vector<std::string> texts;
        std::vector<size_t> owner;
        for (size_t i = 0; i < docs.size(); ++i) {
            if (nodeIds[i] <= 0) continue;
            std::vector<std::string> parts;
            if (opt.chunkWords > 0) parts = chunkWords(docs[i].text, opt.chunkWords, opt.chunkWords / 5);
            else parts.push_back(docs[i].text);
            if (opt.userView) {
                int cw = opt.chunkWords > 0 ? opt.chunkWords : kUserViewWords;
                for (auto& c : chunkWords(firstPersonView(docs[i].text), cw, cw / 5))
                    parts.push_back(std::move(c));
            }
            for (auto& p : parts) { texts.push_back(std::move(p)); owner.push_back(i); }
        }
        auto vecs = pool.embedMany(texts);
        int n = 0;
        for (size_t j = 0; j < vecs.size(); ++j)
            if (!vecs[j].empty()) { docVecs[owner[j]].push_back(std::move(vecs[j])); ++n; }
        if (embedded) *embedded = n;
    }

    const std::string tracePath = gateTracePath();
    auto tokensOf = [&](const std::string& id) {
        auto it = docIdx.find(id);
        return it == docIdx.end() ? std::vector<std::string>{}
                                  : Scorer::instance().tokenize(docs[it->second].text);
    };
    auto topK = [maxK](const std::vector<std::string>& v) {
        return std::vector<std::string>(v.begin(), v.begin() + std::min<size_t>(v.size(), maxK));
    };

    std::vector<std::vector<std::string>> out;
    out.reserve(questions.size());
    for (const auto& q : questions) {
        GateTrace trace;
        std::vector<std::string> ranked;
        if ((q.abstention && !opt.scoreAbstention) || q.evidence.empty()) {
            out.push_back(ranked);
            continue;
        }
        const int64_t asOf = q.as_of > 0 ? q.as_of : std::numeric_limits<int32_t>::max();

        // ---- lexical ----
        auto hits = store.recallAsOf(q.question, asOf, poolN);
        std::vector<std::string> bmIds;
        std::vector<double> bmRaw;
        for (const auto& h : hits) {
            auto it = docOfNode.find(h.id);
            if (it == docOfNode.end()) continue;
            bmIds.push_back(docs[it->second].id);
            bmRaw.push_back(h.bm25_score);
        }
        auto bmNorm = Scorer::normalizeMinMax(bmRaw);
        if (!tracePath.empty() && !bmIds.empty()) {
            trace.bmS1 = bmRaw[0];
            trace.bmS2 = bmRaw.size() > 1 ? bmRaw[1] : 0.0;
            trace.bmCov = queryCoverage(tokensOf(bmIds[0]), Scorer::instance().tokenize(q.question));
        }
        if (opt.prfBeta > 0 && !bmIds.empty())
            applyPrf(q.question, asOf, poolN, docs, docIdx, docOfNode, store, opt.prfBeta,
                     bmIds, bmNorm);

        // ---- dense (full scan of the as-of view, not a BM25 rerank) ----
        std::map<std::string, double> cosOf;
        std::vector<std::string> denseIds;
        if (dense) {
            auto qv = pool.embedOne(q.question);
            std::vector<std::pair<double, size_t>> sc;
            for (size_t i = 0; i < docs.size(); ++i) {
                if (docVecs[i].empty()) continue;
                if (docs[i].date > 0 && docs[i].date > asOf) continue;
                double best = -1.0;
                for (const auto& v : docVecs[i]) best = std::max(best, (double)embed::cosine(qv, v));
                sc.emplace_back(best, i);
            }
            std::stable_sort(sc.begin(), sc.end(),
                             [](const auto& a, const auto& b) { return a.first > b.first; });
            for (const auto& [s, i] : sc) cosOf[docs[i].id] = std::max(0.0, s);
            for (size_t r = 0; r < sc.size() && (int)r < poolN; ++r)
                denseIds.push_back(docs[sc[r].second].id);
        }

        // ---- fusion ----
        std::map<std::string, double> score;
        if (dense && opt.fusion == "rrf") {
            for (auto& [id, s] : rrfFuse({bmIds, denseIds})) score[id] = s;
        } else if (dense) {
            std::map<std::string, double> bm;
            for (size_t i = 0; i < bmIds.size(); ++i) bm[bmIds[i]] = bmNorm[i];
            for (const auto& id : bmIds) score[id] = 0.0;
            for (const auto& id : denseIds) score[id] = 0.0;
            for (auto& [id, s] : score) {
                auto b = bm.find(id);
                auto c = cosOf.find(id);
                s = opt.alpha * (b == bm.end() ? 0.0 : b->second) +
                    (1.0 - opt.alpha) * (c == cosOf.end() ? 0.0 : c->second);
            }
        } else {
            for (size_t i = 0; i < bmIds.size(); ++i) score[bmIds[i]] = bmNorm[i];
        }
        if (score.empty()) { out.push_back(ranked); continue; }
        double top = 0.0;
        for (const auto& [id, s] : score) top = std::max(top, s);

        // ---- temporal window boost ----
        if (opt.temporal > 0) {
            auto w = parseTemporalHint(q.question, asOf);
            if (w.found) {
                for (auto& [id, s] : score) {
                    auto it = docIdx.find(id);
                    int64_t dt = it == docIdx.end() ? 0 : docs[it->second].date;
                    if (dt >= w.lo && dt <= w.hi) s += opt.temporal * top;
                }
            }
        }

        // ---- two-tier: session prior ----
        if (opt.groupBeta > 0) {
            std::map<std::string, std::string> groupOf;
            for (const auto& [id, s] : score) groupOf[id] = benchGroupOf(id);
            score = groupPrior(score, groupOf, opt.groupBeta);
        }

        std::vector<std::pair<std::string, double>> cand(score.begin(), score.end());
        std::stable_sort(cand.begin(), cand.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });

        // ---- MMR over the head ----
        if (dense && opt.mmrLambda < 1.0 && cand.size() > 1) {
            std::vector<std::pair<std::string, double>> head(
                cand.begin(), cand.begin() + std::min<size_t>(cand.size(), kMmrPool));
            std::map<std::string, std::vector<float>> hv;
            for (const auto& [id, s] : head) {
                auto it = docIdx.find(id);
                if (it != docIdx.end() && !docVecs[it->second].empty()) hv[id] = docVecs[it->second][0];
            }
            ranked = mmrSelect(head, hv, opt.mmrLambda, (int)head.size());
            for (size_t i = head.size(); i < cand.size(); ++i) ranked.push_back(cand[i].first);
        } else {
            for (const auto& c : cand) ranked.push_back(c.first);
        }

        // ---- coverage-greedy head (--cover) ----
        if (opt.coverLambda > 0 && ranked.size() > 1) {
            const auto& sc = Scorer::instance();
            const size_t h = std::min<size_t>(ranked.size(), kCoverPool);
            std::vector<std::pair<std::string, double>> head;
            std::map<std::string, std::vector<std::string>> toks;
            for (size_t i = 0; i < h; ++i) {
                head.push_back({ranked[i], score[ranked[i]]});
                auto it = docIdx.find(ranked[i]);
                if (it != docIdx.end()) toks[ranked[i]] = sc.tokenize(docs[it->second].text);
            }
            auto sel = coverSelect(head, toks, sc.tokenize(q.question), opt.coverLambda,
                                   opt.coverLambda * kCoverMuPerLam, (int)h);
            std::copy(sel.begin(), sel.end(), ranked.begin());
        }

        // ---- cross-encoder rerank of the final head ----
        bool gateSkip = false;
        if (!tracePath.empty() && !ranked.empty()) {
            trace.fusedS1 = score[ranked[0]];
            trace.fusedS2 = ranked.size() > 1 ? score[ranked[1]] : 0.0;
            trace.fusedCov = queryCoverage(tokensOf(ranked[0]), Scorer::instance().tokenize(q.question));
            trace.pre = topK(ranked);
        }
        if (rerankOn && opt.rerankGate > 0 && !ranked.empty()) {
            gGateConsidered.fetch_add(1);
            auto it = docIdx.find(ranked[0]);
            const double s2 = ranked.size() > 1 ? score[ranked[1]] : 0.0;
            gateSkip = it != docIdx.end() &&
                       rerankGateSkip(score[ranked[0]], s2,
                                      Scorer::instance().tokenize(docs[it->second].text),
                                      Scorer::instance().tokenize(q.question), opt.rerankGate);
            if (gateSkip) gGateSkipped.fetch_add(1);
        }
        if (rerankOn && !gateSkip && !ranked.empty()) {
            size_t n = std::min<size_t>(ranked.size(), opt.rerankTop);
            if (opt.adjacent > 0) {
                std::map<std::string, std::vector<std::string>> adj;
                for (size_t i = 0; i < n && (int)i < opt.adjacentSeeds; ++i)
                    adj[ranked[i]] = sessionNeighbours(ranked[i], opt.adjacent, docs, docIdx, nodeIds);
                auto ex = adjacentExpandHead(ranked, adj, opt.adjacentSeeds, (int)n);
                ranked = std::move(ex.ids);
                n = (size_t)ex.head;
            }
            std::vector<std::string> texts;
            texts.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                auto it = docIdx.find(ranked[i]);
                texts.push_back(it == docIdx.end() ? std::string() : docs[it->second].text);
            }
            std::vector<float> scores;
            if (opt.rerankChunk > 0) {
                // Max-passage rerank: each doc -> its best kRerankPassages windows
                // (query-overlap pre-select), doc score = max passage logit.
                std::vector<std::string> pass;
                std::vector<size_t> owner;
                for (size_t i = 0; i < n; ++i) {
                    auto ch = chunkWords(texts[i], opt.rerankChunk, opt.rerankChunk / kRerankOverlapDiv);
                    for (size_t c : topOverlapChunks(q.question, ch, kRerankPassages)) {
                        pass.push_back(std::move(ch[c]));
                        owner.push_back(i);
                    }
                }
                scores = maxPerOwner(rerank.score(q.question, pass), owner, n);
            } else {
                scores = rerank.score(q.question, texts);
            }
            ranked = rerankHead(ranked, scores, (int)n);
        }
        if ((int)ranked.size() > maxK) ranked.resize(maxK);
        if (!tracePath.empty()) {
            trace.qid = q.id;
            trace.category = q.category;
            trace.post = ranked;
            trace.gold = q.evidence;
            appendGateTrace(tracePath, trace);
        }
        out.push_back(std::move(ranked));
    }
    return out;
}

RerankGateStats benchRerankGateStats() {
    return {gGateConsidered.load(), gGateSkipped.load()};
}

void resetBenchRerankGateStats() {
    gGateConsidered = 0;
    gGateSkipped = 0;
}

}  // namespace icmg::imem
