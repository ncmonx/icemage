// 2026-11-11: see bench_expand.hpp.
#include "bench_expand.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace icmg::imem {

std::vector<PrfTerm> rm3ExpansionTerms(const std::vector<std::string>& queryTokens,
                                       const std::vector<std::vector<std::string>>& docTokens,
                                       const std::vector<double>& docScores,
                                       int numTerms) {
    if (docTokens.empty() || docTokens.size() != docScores.size() || numTerms <= 0) return {};
    double total = 0.0;
    for (double s : docScores) total += std::max(0.0, s);
    if (total <= 0.0) return {};

    const std::set<std::string> query(queryTokens.begin(), queryTokens.end());
    std::map<std::string, double> w;
    for (size_t i = 0; i < docTokens.size(); ++i) {
        const auto& toks = docTokens[i];
        if (toks.empty()) continue;
        const double pd = std::max(0.0, docScores[i]) / total;
        if (pd <= 0.0) continue;
        std::map<std::string, int> tf;
        for (const auto& t : toks) ++tf[t];
        for (const auto& [t, n] : tf) {
            if (t.size() < kPrfMinTermLen || query.count(t)) continue;
            w[t] += pd * (double)n / (double)toks.size();
        }
    }
    std::vector<PrfTerm> out;
    out.reserve(w.size());
    for (const auto& [t, x] : w) out.push_back({t, x});   // map order = alphabetical
    std::stable_sort(out.begin(), out.end(),
                     [](const PrfTerm& a, const PrfTerm& b) { return a.weight > b.weight; });
    if ((int)out.size() > numTerms) out.resize(numTerms);
    return out;
}

bool rerankGateSkip(double s1, double s2,
                    const std::vector<std::string>& top1Tokens,
                    const std::vector<std::string>& queryTokens, double tau) {
    if (tau <= 0.0 || s1 <= 0.0 || queryTokens.empty()) return false;
    if ((s1 - s2) / s1 < tau) return false;
    const std::set<std::string> top(top1Tokens.begin(), top1Tokens.end());
    for (const auto& t : queryTokens)
        if (!top.count(t)) return false;
    return true;
}

namespace {
double jaccard(const std::set<std::string>& a, const std::set<std::string>& b) {
    if (a.empty() && b.empty()) return 0.0;
    size_t inter = 0;
    for (const auto& x : a) inter += b.count(x);
    return (double)inter / (double)(a.size() + b.size() - inter);
}
}  // namespace

std::vector<std::string> coverSelect(const std::vector<std::pair<std::string, double>>& cand,
                                     const std::map<std::string, std::vector<std::string>>& docTokens,
                                     const std::vector<std::string>& queryTokens,
                                     double lam, double mu, int budget) {
    const size_t n = cand.size();
    if (n == 0) return {};
    const size_t head = std::min<size_t>(n, (size_t)std::max(0, budget));
    double lo = cand[0].second, hi = cand[0].second;
    for (const auto& c : cand) { lo = std::min(lo, c.second); hi = std::max(hi, c.second); }
    const std::set<std::string> q(queryTokens.begin(), queryTokens.end());
    std::vector<std::set<std::string>> terms(n);
    std::vector<double> rel(n);
    for (size_t i = 0; i < n; ++i) {
        auto it = docTokens.find(cand[i].first);
        if (it != docTokens.end()) terms[i].insert(it->second.begin(), it->second.end());
        rel[i] = hi > lo ? (cand[i].second - lo) / (hi - lo) : 1.0;
    }
    std::set<std::string> covered;
    std::vector<bool> used(n, false);
    std::vector<size_t> picked;
    std::vector<std::string> out;
    out.reserve(n);
    for (size_t step = 0; step < head; ++step) {
        size_t best = n;
        double bestS = 0.0;
        for (size_t i = 0; i < n; ++i) {
            if (used[i]) continue;
            size_t fresh = 0;
            for (const auto& t : q) fresh += terms[i].count(t) && !covered.count(t);
            double red = 0.0;
            for (size_t p : picked) red = std::max(red, jaccard(terms[i], terms[p]));
            const double s = rel[i] + (q.empty() ? 0.0 : lam * (double)fresh / (double)q.size()) - mu * red;
            if (best == n || s > bestS) { best = i; bestS = s; }
        }
        used[best] = true;
        picked.push_back(best);
        for (const auto& t : q) if (terms[best].count(t)) covered.insert(t);
        out.push_back(cand[best].first);
    }
    for (size_t i = 0; i < n; ++i) if (!used[i]) out.push_back(cand[i].first);
    return out;
}

}  // namespace icmg::imem
