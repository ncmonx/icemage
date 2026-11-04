// 2026-11-01: see recall_rerank.hpp.
#include "recall_rerank.hpp"
#include "bench_rerank_pool.hpp"
#include "bench_retrieval.hpp"   // chunkWords / topOverlapChunks / maxPerOwner

#include <algorithm>
#include <cstdlib>
#include <numeric>

namespace icmg::imem {

int parseRerankN(const std::string& s) {
    if (s.empty()) return 0;
    try {
        size_t used = 0;
        int v = std::stoi(s, &used);
        if (used != s.size() || v <= 0) return 0;
        return std::min(v, kRecallRerankMax);
    } catch (...) { return 0; }
}

int rerankNFromEnv() {
    const char* e = std::getenv(kRecallRerankEnv);
    return e ? parseRerankN(e) : 0;
}

std::string rerankDocText(const MemoryNode& n) {
    std::string d = n.topic + ": " + n.content;
    if (d.size() > kRecallRerankDocCap) d.resize(kRecallRerankDocCap);
    return d;
}

std::vector<MemoryNode> reorderHeadByScore(std::vector<MemoryNode> nodes,
                                           const std::vector<float>& scores, int n) {
    size_t head = std::min<size_t>(nodes.size(), n > 0 ? (size_t)n : 0);
    if (scores.empty() || scores.size() != head) return nodes;
    std::vector<size_t> idx(head);
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(),
                     [&](size_t a, size_t b) { return scores[a] > scores[b]; });
    std::vector<MemoryNode> out;
    out.reserve(nodes.size());
    for (size_t i : idx) out.push_back(nodes[i]);
    for (size_t i = head; i < nodes.size(); ++i) out.push_back(nodes[i]);
    return out;
}

std::vector<std::string> rerankPassages(const std::string& query, const MemoryNode& n) {
    const std::string text = n.topic + ": " + n.content;
    auto ch = chunkWords(text, kRecallRerankChunkWords, kRecallRerankChunkWords / 4);
    if (ch.empty()) return {rerankDocText(n)};
    std::vector<std::string> out;
    for (size_t i : topOverlapChunks(query, ch, kRecallRerankPassages)) out.push_back(std::move(ch[i]));
    return out;
}

int rerankFetchLimit(int limit, int n) { return n > 0 ? std::max(limit, n) : limit; }

std::vector<MemoryNode> applyRecallRerank(const std::string& query,
                                          std::vector<MemoryNode> cand,
                                          int n, int limit, bool* applied) {
    if (applied) *applied = false;
    if (n > 0 && !cand.empty() && BenchRerankPool::instance().available()) {
        size_t head = std::min<size_t>(cand.size(), (size_t)n);
        // Max-passage scoring: a long memory overflows the 256-token pair, so
        // score its best windows and keep the max (LongMemEval NDCG@10
        // .791 whole-doc -> .863 with 150-word passages).
        std::vector<std::string> pass;
        std::vector<size_t> owner;
        for (size_t i = 0; i < head; ++i)
            for (auto& p : rerankPassages(query, cand[i])) { pass.push_back(std::move(p)); owner.push_back(i); }
        auto scores = maxPerOwner(
            BenchRerankPool::instance().score(query, pass, kRecallRerankWorkers), owner, head);
        if (scores.size() == head) {
            cand = reorderHeadByScore(std::move(cand), scores, (int)head);
            if (applied) *applied = true;
        }
    }
    if (limit > 0 && (int)cand.size() > limit) cand.resize(limit);
    return cand;
}

}  // namespace icmg::imem
