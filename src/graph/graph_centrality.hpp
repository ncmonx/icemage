#pragma once
// PageRank + Personalized PageRank over the icmg code graph (2026-06-12).
//
// Upgrade over degreeCentrality (graph_report.hpp): degree counts only DIRECT
// edges; PageRank propagates importance TRANSITIVELY — a symbol referenced by
// important symbols outranks one referenced by many trivial ones (the ranking
// aider/RepoGraph use for context selection). icmg-native refinement: each
// transition is CONFIDENCE-WEIGHTED via edgeConfidence() so structural edges
// (inherits 0.97) carry more mass than name-based ones (calls 0.70); unresolved
// edges (dst<=0) contribute no transition and their out-mass is treated as
// dangling -> redistributed through the teleport vector.
//
// The personalized variant biases the teleport vector toward task-seed nodes,
// giving task-aware ranking for `icmg pack`. Pure (nodes+edges in,
// map<id,double> out) so it is unit-testable without a DB — same ethos as
// graph_report.hpp. Mass is conserved: sum of scores stays ~1 every iteration.
#include "graph_node.hpp"
#include "graph_report.hpp"   // edgeConfidence
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace icmg::graph {

// Core power-iteration PageRank. `personalization` maps node id -> teleport
// weight (un-normalized; normalized internally). Empty => uniform teleport.
// d = damping factor (0.85 canonical); iters = power-iteration count (code
// graphs are sparse and converge well under 30).
inline std::map<int64_t,double> pageRankCore(
        const std::vector<GraphNode>& nodes,
        const std::vector<GraphEdge>& edges,
        const std::map<int64_t,double>& personalization,
        double d, int iters) {
    std::map<int64_t,double> pr;
    const size_t N = nodes.size();
    if (N == 0) return pr;

    // contiguous index for the node ids
    std::vector<int64_t> ids; ids.reserve(N);
    std::map<int64_t,size_t> idx;
    for (const auto& n : nodes) { idx[n.id] = ids.size(); ids.push_back(n.id); }

    // weighted out-adjacency (only edges whose dst is a resolved in-graph node)
    std::vector<std::vector<std::pair<size_t,double>>> out(N);
    std::vector<double> outSum(N, 0.0);
    for (const auto& e : edges) {
        auto si = idx.find(e.src);
        if (si == idx.end()) continue;
        if (e.dst <= 0) continue;                 // unresolved -> no transition target
        auto di = idx.find(e.dst);
        if (di == idx.end()) continue;            // dst not in this node set
        double w = edgeConfidence(e) * (e.weight > 0 ? e.weight : 1.0);
        if (w <= 0) continue;
        out[si->second].push_back({ di->second, w });
        outSum[si->second] += w;
    }

    // teleport / personalization vector, normalized to sum 1 (uniform if empty)
    std::vector<double> tele(N, 0.0);
    double teleSum = 0.0;
    for (const auto& kv : personalization) {
        auto it = idx.find(kv.first);
        if (it != idx.end() && kv.second > 0) { tele[it->second] += kv.second; teleSum += kv.second; }
    }
    if (teleSum <= 0.0) { for (size_t i = 0; i < N; ++i) tele[i] = 1.0 / (double)N; }
    else                { for (size_t i = 0; i < N; ++i) tele[i] /= teleSum; }

    // power iteration. mass-conserving: sum(next) = (1-d) + d*sum(rank) = 1.
    std::vector<double> rank(N, 1.0 / (double)N), next(N, 0.0);
    for (int t = 0; t < iters; ++t) {
        double dangling = 0.0;
        for (size_t i = 0; i < N; ++i) if (outSum[i] <= 0.0) dangling += rank[i];
        for (size_t i = 0; i < N; ++i)
            next[i] = (1.0 - d) * tele[i] + d * dangling * tele[i];   // base + redistributed dangling
        for (size_t i = 0; i < N; ++i) {
            if (outSum[i] <= 0.0) continue;
            double share = d * rank[i] / outSum[i];
            for (const auto& pe : out[i]) next[pe.first] += share * pe.second;
        }
        rank.swap(next);
        std::fill(next.begin(), next.end(), 0.0);
    }

    for (size_t i = 0; i < N; ++i) pr[ids[i]] = rank[i];
    return pr;
}

// Global PageRank — uniform teleport. Drop-in richer cousin of degreeCentrality.
inline std::map<int64_t,double> pageRank(
        const std::vector<GraphNode>& nodes,
        const std::vector<GraphEdge>& edges,
        double d = 0.85, int iters = 30) {
    return pageRankCore(nodes, edges, {}, d, iters);
}

// Personalized PageRank — teleport biased toward `seed` (id -> weight). Use the
// task's matched nodes as seed for task-aware ranking in `icmg pack`.
inline std::map<int64_t,double> personalizedPageRank(
        const std::vector<GraphNode>& nodes,
        const std::vector<GraphEdge>& edges,
        const std::map<int64_t,double>& seed,
        double d = 0.85, int iters = 30) {
    return pageRankCore(nodes, edges, seed, d, iters);
}

// Build a personalization seed from a free-text task: a node's weight = the
// number of DISTINCT task tokens (lowercased, length >= 3) that appear in its
// symbol_name or path basename. Nodes with no match are absent (uniform-treated
// by personalizedPageRank). Feeding this seed pulls the ranking toward
// task-relevant code instead of global third_party hubs. Pure + testable.
inline std::map<int64_t,double> seedFromTask(const std::vector<GraphNode>& nodes,
                                             const std::string& task) {
    // tokenize task into distinct lowercase alnum tokens of length >= 3
    std::set<std::string> toks;
    std::string cur;
    auto flush = [&]() { if (cur.size() >= 3) toks.insert(cur); cur.clear(); };
    for (char c : task) {
        if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
        else flush();
    }
    flush();

    std::map<int64_t,double> seed;
    if (toks.empty()) return seed;
    for (const auto& n : nodes) {
        std::string hay = n.symbol_name;
        std::string p = n.path;
        size_t sl = p.find_last_of("/\\");
        hay += " ";
        hay += (sl == std::string::npos ? p : p.substr(sl + 1));
        for (char& c : hay) c = (char)std::tolower((unsigned char)c);
        int matches = 0;
        for (const auto& t : toks) if (hay.find(t) != std::string::npos) ++matches;
        if (matches > 0) seed[n.id] = (double)matches;
    }
    return seed;
}

}  // namespace icmg::graph
