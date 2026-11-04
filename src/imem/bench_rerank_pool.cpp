// 2026-10-31: see bench_rerank_pool.hpp.
#include "bench_rerank_pool.hpp"
#include "bench_embed_pool.hpp"

#include "../embed/reranker.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace icmg::imem {

namespace {

struct RerankState {
    std::mutex mu;
    std::vector<std::unique_ptr<embed::Reranker>> workers;
    bool probed = false;
};

RerankState& state() {
    static RerankState s;
    return s;
}

// Caller holds s.mu.
void probe(RerankState& s) {
    if (s.probed) return;
    s.probed = true;
    if (auto r = embed::makeReranker()) s.workers.push_back(std::move(r));
}

// Caller holds s.mu.
void growTo(RerankState& s, int n) {
    while ((int)s.workers.size() < n) {
        auto r = embed::makeReranker();
        if (!r) break;
        s.workers.push_back(std::move(r));
    }
}

}  // namespace

BenchRerankPool& BenchRerankPool::instance() {
    static BenchRerankPool p;
    return p;
}

bool BenchRerankPool::available() {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    probe(s);
    return !s.workers.empty();
}

std::string BenchRerankPool::model() {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    probe(s);
    return s.workers.empty() ? "" : s.workers[0]->model();
}

std::vector<float> BenchRerankPool::score(const std::string& query,
                                          const std::vector<std::string>& docs,
                                          int maxWorkers) {
    auto& s = state();
    std::vector<embed::Reranker*> ws;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        probe(s);
        if (s.workers.empty() || docs.empty()) return {};
        int want = std::min<int>(BenchEmbedPool::instance().threads(), (int)docs.size());
        if (maxWorkers > 0) want = std::min(want, maxWorkers);
        growTo(s, std::max(1, want));
        for (int i = 0; i < want && i < (int)s.workers.size(); ++i) ws.push_back(s.workers[i].get());
    }
    std::vector<float> out(docs.size(), 0.0f);
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    auto work = [&](embed::Reranker* r) {
        for (size_t j; (j = next.fetch_add(1)) < docs.size();) {
            auto v = r->score(query, {docs[j]});
            if (v.size() != 1) { failed = true; return; }
            out[j] = v[0];
        }
    };
    std::vector<std::thread> pool;
    for (size_t t = 1; t < ws.size(); ++t) pool.emplace_back(work, ws[t]);
    work(ws[0]);
    for (auto& th : pool) th.join();
    if (failed) return {};
    return out;
}

}  // namespace icmg::imem
