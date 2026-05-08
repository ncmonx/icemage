// Phase 26 T6: Louvain greedy single-pass.
//
// Modularity Q = (1/2m) Σ_ij [A_ij - k_i*k_j/2m] δ(c_i, c_j)
// Greedy: for each node, try moving to neighbor's community, keep best ΔQ.
// Iterate until no node moves. Single-level (no recursive coarsening) — adequate
// for file-level graphs.
#include "community.hpp"
#include <algorithm>
#include <random>

namespace icmg::graph {

CommunityResult louvain(const AdjList& adj, int max_iter) {
    int N = (int)adj.size();
    CommunityResult res;
    res.cluster_id.assign(N, 0);
    if (N == 0) return res;

    // Init: each node its own community.
    std::vector<int> comm(N);
    for (int i = 0; i < N; ++i) comm[i] = i;

    // Node strength k_i = sum of edge weights incident to i.
    std::vector<double> k(N, 0.0);
    double m_sum = 0.0;
    for (int i = 0; i < N; ++i) {
        for (auto& [j, w] : adj[i]) k[i] += w;
        m_sum += k[i];
    }
    m_sum /= 2.0;
    if (m_sum <= 0.0) {
        // Trivial graph — each isolated.
        for (int i = 0; i < N; ++i) res.cluster_id[i] = i;
        res.cluster_count = N;
        return res;
    }

    // Community total strength Σ_c.
    std::unordered_map<int, double> sigma;
    for (int i = 0; i < N; ++i) sigma[comm[i]] += k[i];

    auto delta_q = [&](int i, int target_c, int curr_c, double k_i_in_curr, double k_i_in_target) {
        // Standard Louvain ΔQ when moving i from curr_c to target_c.
        double s_target = sigma[target_c];
        double s_curr   = sigma[curr_c] - k[i];   // remove i's contribution
        double k_i      = k[i];
        // Gain from joining target:
        double gain = (k_i_in_target - s_target * k_i / m_sum) / m_sum;
        // Loss from leaving current:
        double loss = (k_i_in_curr   - s_curr   * k_i / m_sum) / m_sum;
        return gain - loss;
    };

    bool moved = true;
    int iter = 0;
    while (moved && iter < max_iter) {
        moved = false; ++iter;
        for (int i = 0; i < N; ++i) {
            int curr_c = comm[i];
            // Compute weight from i to each adjacent community.
            std::unordered_map<int, double> w_to;
            for (auto& [j, w] : adj[i]) {
                w_to[comm[j]] += w;
            }
            double k_i_in_curr = w_to.count(curr_c) ? w_to[curr_c] : 0.0;
            int best_c = curr_c;
            double best_dq = 0.0;
            for (auto& [target_c, w] : w_to) {
                if (target_c == curr_c) continue;
                double dq = delta_q(i, target_c, curr_c, k_i_in_curr, w);
                if (dq > best_dq) {
                    best_dq = dq;
                    best_c = target_c;
                }
            }
            if (best_c != curr_c) {
                sigma[curr_c] -= k[i];
                sigma[best_c] += k[i];
                comm[i] = best_c;
                moved = true;
            }
        }
    }

    // Compact cluster ids.
    std::unordered_map<int, int> remap;
    int next_id = 0;
    for (int i = 0; i < N; ++i) {
        int c = comm[i];
        if (!remap.count(c)) remap[c] = next_id++;
        res.cluster_id[i] = remap[c];
    }
    res.cluster_count = next_id;

    // Final modularity.
    double Q = 0.0;
    for (int i = 0; i < N; ++i) {
        for (auto& [j, w] : adj[i]) {
            if (res.cluster_id[i] == res.cluster_id[j]) {
                Q += w - k[i] * k[j] / (2.0 * m_sum);
            }
        }
    }
    Q /= (2.0 * m_sum);
    res.modularity = Q;
    return res;
}

} // namespace icmg::graph
