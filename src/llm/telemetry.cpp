// v1.31.0 B5: LLM telemetry ring-buffer impl. See telemetry.hpp.
#include "telemetry.hpp"

#include <algorithm>
#include <chrono>

namespace icmg::llm {

Telemetry& Telemetry::instance() {
    static Telemetry t;
    return t;
}

void Telemetry::push(const CallSample& s) {
    std::lock_guard<std::mutex> g(mu_);
    if (s.cold_load && !s.ok) ++cold_load_fail_;
    if (ring_.size() < kCap) {
        ring_.push_back(s);
    } else {
        full_ = true;
        ring_[head_] = s;
        head_ = (head_ + 1) % kCap;
    }
}

std::vector<CallSample> Telemetry::snapshot(std::size_t max_n) const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<CallSample> out;
    if (ring_.empty()) return out;
    std::size_t n = std::min(max_n, ring_.size());
    out.reserve(n);
    // Iterate from newest to oldest.
    std::size_t start = full_ ? (head_ + kCap - 1) % kCap
                              : (ring_.size() - 1);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(ring_[(start + kCap - i) % kCap]);
    }
    return out;
}

Telemetry::Stats Telemetry::stats(std::size_t last_n) const {
    auto snap = snapshot(last_n == 0 ? kCap : last_n);
    Stats st;
    st.n = snap.size();
    if (snap.empty()) {
        std::lock_guard<std::mutex> g(mu_);
        st.cold_load_fail_count = cold_load_fail_;
        return st;
    }
    // wall_ms percentiles.
    std::vector<double> w; w.reserve(snap.size());
    std::uint64_t total_out = 0; double total_wall = 0.0;
    std::size_t err = 0;
    for (const auto& s : snap) {
        w.push_back(s.wall_ms);
        total_out  += s.tokens_out;
        total_wall += s.wall_ms;
        if (!s.ok) ++err;
    }
    std::sort(w.begin(), w.end());
    auto pct = [&](double p) {
        std::size_t idx = static_cast<std::size_t>(p * (w.size() - 1));
        return w[idx];
    };
    st.p50_wall_ms = pct(0.50);
    st.p95_wall_ms = pct(0.95);
    st.error_rate  = static_cast<double>(err) / static_cast<double>(snap.size());
    if (total_wall > 0)
        st.avg_tok_per_s = (static_cast<double>(total_out) * 1000.0) / total_wall;
    {
        std::lock_guard<std::mutex> g(mu_);
        st.cold_load_fail_count = cold_load_fail_;
    }
    return st;
}

void Telemetry::clear() {
    std::lock_guard<std::mutex> g(mu_);
    ring_.clear();
    head_ = 0;
    full_ = false;
    cold_load_fail_ = 0;
}

} // namespace icmg::llm
