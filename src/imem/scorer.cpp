#include "scorer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iterator>

namespace icmg::imem {

Scorer& Scorer::instance() {
    static Scorer inst;
    return inst;
}

std::vector<std::string> Scorer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::istringstream ss(text);
    std::string t;
    while (ss >> t) {
        // lowercase
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        // strip leading/trailing punctuation
        while (!t.empty() && !std::isalnum((unsigned char)t.front())) t.erase(t.begin());
        while (!t.empty() && !std::isalnum((unsigned char)t.back()))  t.pop_back();
        if (t.size() >= 2) tokens.push_back(t); // skip single-char tokens
    }
    return tokens;
}

std::string Scorer::document(const MemoryNode& node) const {
    // Concatenate searchable fields; topic gets extra weight via repetition
    return node.topic + " " + node.topic + " " + node.content + " " + node.keywords;
}

void Scorer::fit(const std::vector<MemoryNode>& corpus) {
    if (!dirty_) return;

    df_.clear();
    N_     = (int)corpus.size();
    avgdl_ = 0.0;

    for (auto& node : corpus) {
        auto tokens = tokenize(document(node));
        avgdl_ += tokens.size();

        // Unique terms per doc
        std::vector<std::string> sorted_tokens = tokens;
        std::sort(sorted_tokens.begin(), sorted_tokens.end());
        sorted_tokens.erase(std::unique(sorted_tokens.begin(), sorted_tokens.end()),
                            sorted_tokens.end());
        for (auto& t : sorted_tokens) df_[t]++;
    }

    if (N_ > 0) avgdl_ /= N_;
    dirty_ = false;
}

double Scorer::idf(const std::string& term) const {
    auto it = df_.find(term);
    int df = (it == df_.end()) ? 0 : it->second;
    // Smoothed IDF: log((N+1)/(df+1) + 1)
    return std::log(static_cast<double>(N_ + 1) / (df + 1) + 1.0);
}

double Scorer::bm25(const std::string& query, const MemoryNode& node) const {
    auto q_tokens = tokenize(query);
    auto d_tokens = tokenize(document(node));
    double dl = static_cast<double>(d_tokens.size());

    // Build term-frequency map for document
    std::unordered_map<std::string, int> tf_map;
    for (auto& t : d_tokens) tf_map[t]++;

    // v1.64 T1: hoist loop-invariants out of the per-query-token loop.
    // The doc-length normalization term and (k1+1) do not depend on the
    // query token, so compute them once instead of every iteration.
    // Result is bit-identical to the prior form (same operations, fewer
    // times) — verified by parity test.
    const double len_norm = k1_ * (1.0 - b_ + b_ * (avgdl_ > 0 ? dl / avgdl_ : 1.0));
    const double k1p1     = k1_ + 1.0;

    double score = 0.0;
    for (auto& qt : q_tokens) {
        auto it = tf_map.find(qt);
        if (it == tf_map.end()) continue;
        double tf = static_cast<double>(it->second);
        double idf_val = idf(qt);
        double denom = tf + len_norm;
        score += idf_val * (tf * k1p1) / denom;
    }
    return score;
}

double Scorer::recencyDecay(int64_t last_used) const {
    if (last_used <= 0) return 0.5; // never used → moderate decay
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    double hours = static_cast<double>(now - last_used) / 3600.0;
    return std::exp(-0.01 * hours);
}

// v1.20.0 (Task M1): access-aware decay. Hot memories (frequently recalled)
// decay slower than cold ones. Formula: decay_factor = 1 / (1 + freq*0.1).
// freq=0 → factor=1.0 (no boost); freq=10 → 0.5 (decay rate halved);
// freq=100 → 0.09 (decay nearly suppressed). Multiplied INTO recencyDecay
// (effectively flattens the exponential curve for hot memos).
double Scorer::accessAwareDecay(int64_t last_used, int freq) const {
    double base = recencyDecay(last_used);
    if (freq <= 0) return base;
    // Approach 1.0 asymptotically as freq grows. Bounded so very-hot memos
    // don't escape decay entirely.
    double boost = 1.0 / (1.0 + freq * 0.1);
    return base + (1.0 - base) * (1.0 - boost);
}

// Phase 67 T15: age-based exponential decay envelope (created_at, days).
// Complements recencyDecay (which resets on use): even frequently-used
// memories get demoted if their underlying knowledge is months old, since
// codebase reality drifts. Half-life ≈ 90 days at λ=0.0077.
//
// v1.21.9 (M2): tier-aware decay rate. Critical memories never decay
// (λ=0); high decays at half the medium rate (~180d half-life); low at
// double (~45d). See importanceDecayMultiplier in memory_node.hpp.
double Scorer::ageDecay(int64_t created_at, int importance) const {
    if (created_at <= 0) return 1.0;  // unknown age → no penalty
    double tier_mult = imem::importanceDecayMultiplier(importance);
    if (tier_mult <= 0.0) return 1.0;  // critical — frozen
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    double days = static_cast<double>(now - created_at) / 86400.0;
    return std::exp(-0.0077 * tier_mult * days);
}

double Scorer::score(const std::string& query, const MemoryNode& node) const {
    auto d = scoreDetailed(query, node);
    return d.total;
}

Scorer::ScoreDetail Scorer::scoreDetailed(const std::string& query,
                                           const MemoryNode& node) const {
    ScoreDetail d;
    d.bm25           = bm25(query, node);
    // v1.20.0 (M1): access-aware decay — hot memos decay slower.
    d.recency        = accessAwareDecay(node.last_used, node.frequency);
    // log(2 + freq): floor at log(2)≈0.69 so unvisited nodes (freq=0) are still rankable.
    d.freq           = std::log(2.0 + node.frequency);

    static const double mult[4] = {0.5, 1.0, 1.5, 2.0};
    int imp = std::max(0, std::min(3, node.importance));
    d.importance_mult = mult[imp];

    // Phase 67 T15: age envelope multiplied in.
    // v1.21.9 (M2): tier-aware — critical never decays, high half rate, low double.
    double age_mult = ageDecay(node.created_at, node.importance);

    // Phase 75: pinned (decision-anchor) boost. Pinned memory always wins over
    // recency-only ranking — anti-cognition-drift defense.
    double pin_mult = node.pinned ? 10.0 : 1.0;

    d.total = d.bm25 * d.recency * d.freq * d.importance_mult
            * node.feedback_bias * age_mult * pin_mult;

    // Matched tokens
    auto q_tokens = tokenize(query);
    auto d_tokens = tokenize(document(node));
    std::sort(d_tokens.begin(), d_tokens.end());
    for (auto& qt : q_tokens) {
        if (std::binary_search(d_tokens.begin(), d_tokens.end(), qt))
            d.matched_tokens.push_back(qt);
    }

    return d;
}

std::vector<MemoryNode> Scorer::rank(const std::string& query,
                                      std::vector<MemoryNode> nodes,
                                      int limit) const {
    for (auto& n : nodes) {
        auto d = scoreDetailed(query, n);
        n.score          = d.total;
        n.bm25_score     = d.bm25;
        n.recency        = d.recency;
        n.freq_score     = d.freq;
        n.importance_mult = d.importance_mult;
    }

    // Stable sort by score descending
    std::stable_sort(nodes.begin(), nodes.end(),
        [](const MemoryNode& a, const MemoryNode& b) { return a.score > b.score; });

    // Remove zero-score nodes (no query match)
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
        [](const MemoryNode& n) { return n.score <= 0.0; }), nodes.end());

    if ((int)nodes.size() > limit) nodes.resize(limit);
    return nodes;
}

} // namespace icmg::imem
