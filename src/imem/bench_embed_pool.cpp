// 2026-10-31: see bench_embed_pool.hpp.
#include "bench_embed_pool.hpp"

#include "../embed/embedder.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace icmg::imem {

namespace {

constexpr int kMaxThreads = 8;

struct PoolState {
    std::mutex mu;  // guards workers + cache
    std::vector<std::unique_ptr<embed::Embedder>> workers;
    bool probed = false;
    bool ok = false;
    int dim = 0;
    std::string model;
    std::unordered_map<std::string, std::vector<float>> cache;
};

PoolState& state() {
    static PoolState s;
    return s;
}

// Caller holds s.mu.
void probe(PoolState& s) {
    if (s.probed) return;
    s.probed = true;
    auto e = embed::makeEmbedder();
    if (!e || !e->available()) return;
    s.ok = true;
    s.dim = e->dim();
    s.model = e->model();
    s.workers.push_back(std::move(e));
}

// Caller holds s.mu.
void growTo(PoolState& s, int n) {
    while ((int)s.workers.size() < n) {
        auto e = embed::makeEmbedder();
        if (!e || !e->available()) break;
        s.workers.push_back(std::move(e));
    }
}

}  // namespace

BenchEmbedPool& BenchEmbedPool::instance() {
    static BenchEmbedPool p;
    return p;
}

int BenchEmbedPool::threads() const {
    if (const char* e = std::getenv("ICMG_BENCH_THREADS")) {
        int n = std::atoi(e);
        if (n > 0) return std::min(n, 32);
    }
    int hw = (int)std::thread::hardware_concurrency();
    return std::clamp(hw - 2, 1, kMaxThreads);
}

bool BenchEmbedPool::available() {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    probe(s);
    return s.ok;
}

int BenchEmbedPool::dim() {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    probe(s);
    return s.dim;
}

std::string BenchEmbedPool::model() {
    auto& s = state();
    std::lock_guard<std::mutex> lk(s.mu);
    probe(s);
    return s.model;
}

std::vector<std::vector<float>> BenchEmbedPool::embedMany(const std::vector<std::string>& texts) {
    auto& s = state();
    std::vector<std::vector<float>> out(texts.size());
    std::vector<size_t> todo;  // indices of the first occurrence of each missing text
    std::vector<embed::Embedder*> ws;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        probe(s);
        if (!s.ok) return out;
        std::unordered_map<std::string, size_t> firstIdx;
        for (size_t i = 0; i < texts.size(); ++i) {
            auto it = s.cache.find(texts[i]);
            if (it != s.cache.end()) { out[i] = it->second; continue; }
            if (firstIdx.emplace(texts[i], i).second) todo.push_back(i);
        }
        if (todo.empty()) return out;
        int want = std::min<int>(threads(), (int)todo.size());
        growTo(s, std::max(1, want));
        for (int i = 0; i < want && i < (int)s.workers.size(); ++i)
            ws.push_back(s.workers[i].get());
    }

    // Workers are only touched by this call (bench is single-caller), so each
    // thread owns one embedder without locking.
    std::atomic<size_t> next{0};
    auto work = [&](embed::Embedder* e) {
        for (size_t j; (j = next.fetch_add(1)) < todo.size();)
            out[todo[j]] = e->embed(texts[todo[j]]);
    };
    std::vector<std::thread> pool;
    for (size_t t = 1; t < ws.size(); ++t) pool.emplace_back(work, ws[t]);
    work(ws[0]);
    for (auto& th : pool) th.join();

    std::lock_guard<std::mutex> lk(s.mu);
    for (size_t idx : todo) s.cache.emplace(texts[idx], out[idx]);
    for (size_t i = 0; i < texts.size(); ++i)
        if (out[i].empty()) {
            auto it = s.cache.find(texts[i]);
            if (it != s.cache.end()) out[i] = it->second;
        }
    return out;
}

std::vector<float> BenchEmbedPool::embedOne(const std::string& text) {
    auto v = embedMany({text});
    return v.empty() ? std::vector<float>{} : std::move(v[0]);
}

}  // namespace icmg::imem
