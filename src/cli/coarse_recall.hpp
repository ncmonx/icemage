// 2026-08-25 brain v2.22 #4: coarse-to-fine recall rendering (arXiv 2508.15305
// grounded-memory insight). When a recall result set would burn a large token
// budget, keep the strongest hits FULL and collapse the tail to 1-line index
// rows (progressive disclosure already exists via --index/--get; this applies
// it automatically to the default view's tail). Pure decision helper.
#pragma once
#include <cstdint>
#include <vector>
#include "../imem/memory_node.hpp"
#include "../core/token_budget.hpp"

namespace icmg::cli {

// How many leading nodes stay full-bodied. Returns nodes.size() (== no
// collapse) while the whole set fits in budget_tok. Otherwise: keep at least
// min_full, then extend while the running full-body token sum still fits.
// The collapsed tail costs ~index-line tokens, deliberately ignored (tiny).
inline size_t coarseKeepCount(const std::vector<imem::MemoryNode>& nodes,
                              int64_t budget_tok = 1200,
                              size_t  min_full   = 3) {
    if (nodes.empty()) return 0;
    int64_t total = 0;
    for (auto& n : nodes) total += core::estimateTokens(n.content);
    if (total <= budget_tok) return nodes.size();
    size_t keep = 0;
    int64_t used = 0;
    for (auto& n : nodes) {
        int64_t t = core::estimateTokens(n.content);
        if (keep >= min_full && used + t > budget_tok) break;
        used += t;
        ++keep;
    }
    if (keep < min_full) keep = min_full < nodes.size() ? min_full : nodes.size();
    return keep;
}

} // namespace icmg::cli
