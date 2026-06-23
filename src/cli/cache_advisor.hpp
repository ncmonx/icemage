#pragma once
// cache_advisor.hpp -- cache-hit rate as a TREND, not a snapshot (#1b).
//
// `icmg savings` already shows the aggregate cache-hit rate from the token
// ledger. The advisor goes one step further: it reads per-turn samples, splits
// them chronologically into a PRIOR vs RECENT half, and compares the mean
// hit-rate. A meaningful drop is the tell that volatile content (a per-turn
// timestamp, a memory inject that changes every turn) has crept into what should
// be a STABLE cached prefix -- busting the KV-cache so cached input (~10% price)
// is re-billed as fresh input (~100%). Finding: KV-cache hit rate is the #1 cost
// lever (Manus context-engineering). The advisor names the regression so the
// prefix can be re-stabilized; loop = measure -> optimize -> measure.
//
// Pure + header-only (no DB/IO) so it is fully unit-testable. The token-ledger
// read + the `icmg savings --cache-advisor` surface live in the command.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::cli {

// One per-turn usage sample (a token_ledger row, minus the bits we don't need).
struct CacheSample {
    int64_t ts = 0;
    int64_t input = 0;            // fresh input tokens
    int64_t cache_read = 0;       // served from KV-cache (~10% priced)
    int64_t cache_creation = 0;   // cache-write
};

// Per-sample cache-hit rate = cache_read / total_input. 0 when no input (guards
// divide-by-zero). Range 0..1.
inline double cacheHitOf(const CacheSample& s) {
    const int64_t denom = s.input + s.cache_read + s.cache_creation;
    if (denom <= 0) return 0.0;
    return (double)s.cache_read / (double)denom;
}

struct CacheTrend {
    enum Verdict { NoData, Improving, Stable, Degrading };
    Verdict verdict = NoData;
    double priorRate = 0.0;    // mean hit-rate, older half
    double recentRate = 0.0;   // mean hit-rate, newer half
    double delta = 0.0;        // recentRate - priorRate
    int priorN = 0;
    int recentN = 0;
};

// Split samples chronologically into prior/recent halves, compare mean hit-rate.
// `degradeThresh` (default -0.05) and `improveThresh` (default +0.05) bound the
// "Stable" dead-zone so small noise doesn't flap the verdict. Needs >=2 samples
// (one per half) or returns NoData.
inline CacheTrend analyzeCacheTrend(std::vector<CacheSample> samples,
                                    double degradeThresh = -0.05,
                                    double improveThresh = 0.05) {
    CacheTrend t;
    if (samples.size() < 2) return t;  // NoData
    std::sort(samples.begin(), samples.end(),
              [](const CacheSample& a, const CacheSample& b) { return a.ts < b.ts; });
    const std::size_t n = samples.size();
    const std::size_t mid = n / 2;  // prior = [0,mid), recent = [mid,n)
    double pSum = 0.0, rSum = 0.0;
    for (std::size_t i = 0; i < mid; ++i) pSum += cacheHitOf(samples[i]);
    for (std::size_t i = mid; i < n; ++i) rSum += cacheHitOf(samples[i]);
    t.priorN = (int)mid;
    t.recentN = (int)(n - mid);
    t.priorRate = t.priorN ? pSum / t.priorN : 0.0;
    t.recentRate = t.recentN ? rSum / t.recentN : 0.0;
    t.delta = t.recentRate - t.priorRate;
    if (t.delta <= degradeThresh) t.verdict = CacheTrend::Degrading;
    else if (t.delta >= improveThresh) t.verdict = CacheTrend::Improving;
    else t.verdict = CacheTrend::Stable;
    return t;
}

inline std::string pctStr(double r) {
    int p = (int)(r * 100.0 + 0.5);
    return std::to_string(p) + "%";
}

// Human advice line. Empty for NoData. Degrading carries the actionable prefix-
// stability hint (the whole point of the advisor).
inline std::string formatCacheAdvice(const CacheTrend& t) {
    switch (t.verdict) {
        case CacheTrend::NoData:
            return "";
        case CacheTrend::Stable:
            return "Cache-hit stabil di " + pctStr(t.recentRate) +
                   " -- prefix prompt konsisten, KV-cache kepakai. Pertahankan.";
        case CacheTrend::Improving:
            return "Cache-hit NAIK " + pctStr(t.priorRate) + " -> " + pctStr(t.recentRate) +
                   " -- prompt makin cache-friendly. Mantap.";
        case CacheTrend::Degrading:
            return "Cache-hit TURUN " + pctStr(t.priorRate) + " -> " + pctStr(t.recentRate) +
                   " (-" + pctStr(-t.delta) + "). Kemungkinan konten VOLATIL bocor ke "
                   "cached prefix (timestamp per-turn, memory-inject yang berubah tiap turn) "
                   "-> KV-cache bust, input cached (~10% harga) ke-rebill jadi fresh (~100%). "
                   "Cek: taruh konten volatil di AKHIR user-turn, bukan awal system-prompt; "
                   "jaga prefix append-only.";
    }
    return "";
}

}  // namespace icmg::cli
