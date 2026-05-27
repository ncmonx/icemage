// v1.55 Sub-D D7: Leiden community detection — implementation.
//
// Implements the three-phase Leiden algorithm. See leiden.hpp for the
// public contract. Modularity uses the resolution-adjusted form:
//
//   Q = (1 / 2m) * sum_ij [ A_ij - gamma * (k_i k_j) / (2m) ] * delta(c_i, c_j)
//
// where m = total edge weight (each undirected edge contributes its weight
// once to m, but twice to the symmetric adjacency sums), k_i = strength of
// node i.

#include "leiden.hpp"

#include <algorithm>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>
#include <cmath>

namespace icmg::graph {

namespace {

// Compact adjacency: for each node i, list of (neighbor, weight).
// Self-loops kept; double-listed if input has both (u,v) and (v,u).
struct AdjGraph {
    int N = 0;
    std::vector<std::vector<std::pair<int, double>>> adj;
    std::vector<double> strength;   // k_i = sum of incident edge weights (self-loops count once)
    double m2 = 0.0;                // 2m  = total of strength[i]   (sum_i k_i)

    void rebuild() {
        strength.assign(N, 0.0);
        for (int i = 0; i < N; ++i) {
            for (const auto& [j, w] : adj[i]) strength[i] += w;
        }
        m2 = 0.0;
        for (double s : strength) m2 += s;
    }
};

static AdjGraph buildGraph(int N, const std::vector<LeidenEdge>& edges) {
    AdjGraph g;
    g.N = N;
    g.adj.assign(N, {});
    for (const auto& e : edges) {
        if (e.u < 0 || e.u >= N || e.v < 0 || e.v >= N) continue;
        if (e.w <= 0.0) continue;
        if (e.u == e.v) {
            // self-loop: contributes e.w to strength once (we'll add once below)
            g.adj[e.u].push_back({e.v, e.w});
        } else {
            g.adj[e.u].push_back({e.v, e.w});
            g.adj[e.v].push_back({e.u, e.w});
        }
    }
    g.rebuild();
    return g;
}

// Compute modularity Q for current partition.
static double computeModularity(const AdjGraph& g,
                                 const std::vector<int>& cluster,
                                 double gamma) {
    if (g.m2 <= 0.0) return 0.0;
    // Sum over edges: contribution = (A_ij - gamma * k_i k_j / 2m) * 1[same cluster]
    // Walk adjacency; A_ij counted via stored weights (already symmetric).
    double q_edges = 0.0;
    double q_null  = 0.0;
    for (int i = 0; i < g.N; ++i) {
        for (const auto& [j, w] : g.adj[i]) {
            if (cluster[i] == cluster[j]) q_edges += w;
        }
    }
    // strength-based null model: sum over i of k_i^2 only for nodes in same cluster.
    // Per Newman: sum_c (sum_{i in c} k_i)^2 / (2m)^2  scaled by gamma.
    std::unordered_map<int, double> cluster_strength;
    for (int i = 0; i < g.N; ++i) cluster_strength[cluster[i]] += g.strength[i];
    for (const auto& [c, ks] : cluster_strength) q_null += ks * ks;
    return (q_edges / g.m2) - gamma * (q_null / (g.m2 * g.m2));
}

// Local moving: greedily move each node to the neighbour community that
// maximises ΔQ. Repeat until a full sweep yields no move.
// Returns true if any move happened.
static bool moveNodesFast(const AdjGraph& g,
                          std::vector<int>& cluster,
                          double gamma,
                          std::mt19937& rng) {
    const int N = g.N;
    // Cluster-strength index (sum k_i for nodes currently in cluster c).
    std::unordered_map<int, double> cs;
    for (int i = 0; i < N; ++i) cs[cluster[i]] += g.strength[i];

    std::vector<int> order(N);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);

    bool any_move = false;
    bool changed  = true;
    int  guard    = 0;
    while (changed && guard++ < 64) {
        changed = false;
        for (int i : order) {
            const int c_i = cluster[i];
            const double k_i = g.strength[i];
            // Weight from i to each neighbour-community (excluding self).
            std::unordered_map<int, double> w_to;
            for (const auto& [j, w] : g.adj[i]) {
                if (j == i) continue;
                w_to[cluster[j]] += w;
            }
            // Candidate communities: current + every neighbour-community.
            double best_dq = 0.0;
            int    best_c  = c_i;
            // Removing i from c_i: ΔQ_remove = - [ (k_iC_i - k_i*0)/m - gamma*(k_i*(K_ci - k_i))/(2m^2) ]
            // We'll just compute candidate Q-change as: ΔQ(move i to c) - ΔQ(stay in c_i).
            // Use the standard simplification:
            //   gain(c) = (w_to[c] / m) - gamma * k_i * (K_c - [c==c_i?k_i:0]) / (2m^2)
            // and we want gain(best) - gain(c_i).
            const double inv_m  = 1.0 / g.m2;
            const double inv_2m2 = 1.0 / (g.m2 * g.m2);

            auto gain = [&](int c) -> double {
                double wc = 0.0;
                auto it = w_to.find(c);
                if (it != w_to.end()) wc = it->second;
                double Kc = cs[c];
                if (c == c_i) Kc -= k_i;  // pretend i removed
                return wc * inv_m - gamma * k_i * Kc * inv_2m2;
            };

            const double base = gain(c_i);
            for (const auto& [c, _] : w_to) {
                if (c == c_i) continue;
                double dq = gain(c) - base;
                if (dq > best_dq + 1e-12) {
                    best_dq = dq;
                    best_c  = c;
                }
            }
            if (best_c != c_i) {
                cs[c_i] -= k_i;
                cs[best_c] += k_i;
                cluster[i] = best_c;
                changed = true;
                any_move = true;
            }
        }
    }
    return any_move;
}

// Refinement: per current community, sub-partition into well-connected pieces.
// Each node starts in its own singleton; merge greedily only with neighbours
// that are inside the SAME outer community, and only if they are
// "well-connected" (edge weight to candidate >= gamma * k_i * K_cand / 2m).
// This is the Leiden guarantee over Louvain.
static void refinePartition(const AdjGraph& g,
                            const std::vector<int>& outer,
                            std::vector<int>& refined,
                            double gamma,
                            std::mt19937& rng) {
    const int N = g.N;
    refined.assign(N, 0);
    for (int i = 0; i < N; ++i) refined[i] = i;  // singleton start

    std::unordered_map<int, double> cs;
    for (int i = 0; i < N; ++i) cs[refined[i]] += g.strength[i];

    std::vector<int> order(N);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);

    const double inv_m   = 1.0 / g.m2;
    const double inv_2m2 = 1.0 / (g.m2 * g.m2);

    for (int i : order) {
        const int outer_i = outer[i];
        const int r_i = refined[i];
        const double k_i = g.strength[i];

        // Only consider neighbours within the same outer community.
        std::unordered_map<int, double> w_to;
        for (const auto& [j, w] : g.adj[i]) {
            if (j == i) continue;
            if (outer[j] != outer_i) continue;
            w_to[refined[j]] += w;
        }
        if (w_to.empty()) continue;

        // Well-connected filter + best gain.
        double best_dq = 0.0;
        int    best_c  = r_i;
        auto gain = [&](int c) -> double {
            double wc = 0.0;
            auto it = w_to.find(c);
            if (it != w_to.end()) wc = it->second;
            double Kc = cs[c];
            if (c == r_i) Kc -= k_i;
            return wc * inv_m - gamma * k_i * Kc * inv_2m2;
        };
        const double base = gain(r_i);
        for (const auto& [c, wc] : w_to) {
            if (c == r_i) continue;
            // Well-connectedness: edge weight to candidate must exceed null expectation.
            if (wc < gamma * k_i * cs[c] * inv_2m2 * g.m2) continue;
            double dq = gain(c) - base;
            if (dq > best_dq + 1e-12) {
                best_dq = dq;
                best_c  = c;
            }
        }
        if (best_c != r_i) {
            cs[r_i]    -= k_i;
            cs[best_c] += k_i;
            refined[i] = best_c;
        }
    }
}

// Aggregate: build a new graph where each refined community becomes one node.
// Returns mapping from refined-community-id -> new-node-id, and the new graph.
static AdjGraph aggregate(const AdjGraph& g,
                          const std::vector<int>& refined,
                          std::unordered_map<int, int>& new_id) {
    new_id.clear();
    int next = 0;
    for (int i = 0; i < g.N; ++i) {
        if (!new_id.count(refined[i])) new_id[refined[i]] = next++;
    }
    AdjGraph h;
    h.N = next;
    h.adj.assign(next, {});
    // Sum edge weights between new nodes (self-loops accumulate intra-community weight).
    std::vector<std::unordered_map<int, double>> acc(next);
    for (int i = 0; i < g.N; ++i) {
        int ui = new_id[refined[i]];
        for (const auto& [j, w] : g.adj[i]) {
            int vj = new_id[refined[j]];
            acc[ui][vj] += w;
        }
    }
    for (int u = 0; u < next; ++u) {
        for (const auto& [v, w] : acc[u]) {
            // Each undirected edge appears twice in acc (once per endpoint);
            // emit only u<=v to avoid double-counting, then push twice into adj.
            if (u == v) {
                // self-loop: acc has it once (only when i,j both map to u)
                h.adj[u].push_back({u, w * 0.5});  // halve because we counted i->j AND j->i
            } else if (u < v) {
                h.adj[u].push_back({v, w});
                h.adj[v].push_back({u, w});
            }
        }
    }
    h.rebuild();
    return h;
}

} // namespace

LeidenResult leidenCluster(int num_nodes,
                           const std::vector<LeidenEdge>& edges,
                           const LeidenOptions& opts) {
    LeidenResult R;
    if (num_nodes <= 0) return R;

    std::mt19937 rng(opts.seed);
    AdjGraph g = buildGraph(num_nodes, edges);

    // Initial: each node its own community.
    std::vector<int> cluster(num_nodes);
    std::iota(cluster.begin(), cluster.end(), 0);

    // Track mapping from original node -> current aggregated node.
    std::vector<int> orig_to_curr(num_nodes);
    std::iota(orig_to_curr.begin(), orig_to_curr.end(), 0);

    AdjGraph curr = g;
    double prev_q = computeModularity(curr, cluster, opts.resolution);

    int pass = 0;
    for (; pass < opts.max_iter; ++pass) {
        // Phase 1: move nodes.
        bool moved = moveNodesFast(curr, cluster, opts.resolution, rng);

        // Phase 2: refine.
        std::vector<int> refined;
        if (opts.refine) {
            refinePartition(curr, cluster, refined, opts.resolution, rng);
        } else {
            refined = cluster;
        }

        // Project current "cluster" assignment back to original nodes.
        // First, propagate cluster (outer) to original nodes via orig_to_curr.
        std::vector<int> outer_orig(num_nodes);
        for (int i = 0; i < num_nodes; ++i) outer_orig[i] = cluster[orig_to_curr[i]];
        R.cluster = outer_orig;

        if (!moved) break;

        // Check ΔQ.
        double q_now = computeModularity(curr, cluster, opts.resolution);
        if (q_now - prev_q < opts.tol_dq && pass > 0) {
            prev_q = q_now;
            break;
        }
        prev_q = q_now;

        // Phase 3: aggregate using refined partition. Outer cluster carries
        // through: each aggregated node inherits the outer-community label of
        // its constituent refined community.
        std::unordered_map<int, int> new_id;
        AdjGraph agg = aggregate(curr, refined, new_id);

        // New cluster vector for the aggregated graph: each aggregated node
        // belongs to the outer community of any of its constituents.
        std::vector<int> agg_cluster(agg.N, -1);
        for (int i = 0; i < curr.N; ++i) {
            int n = new_id[refined[i]];
            agg_cluster[n] = cluster[i];
        }

        // Update orig_to_curr: orig node -> refined -> new aggregated id.
        for (int i = 0; i < num_nodes; ++i) {
            orig_to_curr[i] = new_id[refined[orig_to_curr[i]]];
        }

        curr    = std::move(agg);
        cluster = std::move(agg_cluster);
    }

    // Compact cluster ids to [0, K).
    std::unordered_map<int, int> remap;
    int next = 0;
    for (int& c : R.cluster) {
        auto it = remap.find(c);
        if (it == remap.end()) { remap[c] = next; c = next++; }
        else                    c = it->second;
    }
    R.num_clusters = next;
    R.modularity   = computeModularity(g, R.cluster, opts.resolution);
    R.passes       = pass + 1;
    return R;
}

} // namespace icmg::graph
