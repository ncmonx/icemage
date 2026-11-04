// 2026-10-29: retrieval metrics for standard memory benchmarks (LongMemEval
// reports recall_any@k / recall_all@k / NDCG@k on evidence ids). Pure math.
#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace icmg::imem {

// 1.0 if ANY gold id is in the top-k, else 0.0.
inline double recallAnyAtK(const std::vector<std::string>& ranked,
                           const std::vector<std::string>& gold, int k) {
    std::set<std::string> g(gold.begin(), gold.end());
    for (int i = 0; i < k && i < (int)ranked.size(); ++i)
        if (g.count(ranked[i])) return 1.0;
    return 0.0;
}

// 1.0 if ALL gold ids are in the top-k (multi-evidence questions), else 0.0.
inline double recallAllAtK(const std::vector<std::string>& ranked,
                           const std::vector<std::string>& gold, int k) {
    std::set<std::string> g(gold.begin(), gold.end());
    if (g.empty()) return 0.0;
    std::set<std::string> top;
    for (int i = 0; i < k && i < (int)ranked.size(); ++i) top.insert(ranked[i]);
    for (const auto& id : g) if (!top.count(id)) return 0.0;
    return 1.0;
}

// Binary-relevance NDCG@k.
inline double ndcgAtK(const std::vector<std::string>& ranked,
                      const std::vector<std::string>& gold, int k) {
    std::set<std::string> g(gold.begin(), gold.end());
    if (g.empty()) return 0.0;
    double dcg = 0.0;
    for (int i = 0; i < k && i < (int)ranked.size(); ++i)
        if (g.count(ranked[i])) dcg += 1.0 / std::log2((double)i + 2.0);
    double idcg = 0.0;
    int ideal = std::min<int>(k, (int)g.size());
    for (int i = 0; i < ideal; ++i) idcg += 1.0 / std::log2((double)i + 2.0);
    return idcg > 0.0 ? dcg / idcg : 0.0;
}

struct BenchQResult {
    std::string id;
    std::string category;
    bool        skipped = false;   // abstention / no evidence -> not scored
    std::vector<std::string> ranked;
    std::map<int, double> recall_any, recall_all, ndcg;
};

struct BenchCatAgg {
    int n = 0;
    std::map<int, double> recall_any, recall_all, ndcg;  // means
};

struct BenchAgg {
    int scored = 0;
    int skipped = 0;
    BenchCatAgg overall;
    std::map<std::string, BenchCatAgg> by_category;
};

inline BenchAgg aggregateBench(const std::vector<BenchQResult>& rs,
                               const std::vector<int>& ks) {
    BenchAgg a;
    auto add = [&](BenchCatAgg& c, const BenchQResult& r) {
        ++c.n;
        for (int k : ks) {
            auto get = [&](const std::map<int, double>& m) {
                auto it = m.find(k); return it == m.end() ? 0.0 : it->second;
            };
            c.recall_any[k] += get(r.recall_any);
            c.recall_all[k] += get(r.recall_all);
            c.ndcg[k]       += get(r.ndcg);
        }
    };
    auto finish = [&](BenchCatAgg& c) {
        if (c.n == 0) return;
        for (int k : ks) {
            c.recall_any[k] /= c.n; c.recall_all[k] /= c.n; c.ndcg[k] /= c.n;
        }
    };
    for (const auto& r : rs) {
        if (r.skipped) { ++a.skipped; continue; }
        ++a.scored;
        add(a.overall, r);
        add(a.by_category[r.category], r);
    }
    finish(a.overall);
    for (auto& [_, c] : a.by_category) finish(c);
    return a;
}

}  // namespace icmg::imem
