#pragma once
// v2.1 C3: U-shaped ordering — mitigate "lost-in-the-middle".
//
// LLMs attend most reliably to content at the START and END of the context
// window and worst to content in the MIDDLE (Liu et al. 2023, "Lost in the
// Middle"). When we inject a list of context slices already ranked DESC by
// importance, feeding them in plain rank order buries the #2/#3 items in the
// low-attention middle. reorderUShaped() instead lays importance out as a U:
// the highest-ranked items take the two edges (front + back), the lowest sink
// to the center. Reading the result front-to-back, importance falls then
// rises — high, ..., low, ..., high.
//
// Placement (input MUST be pre-sorted DESC by importance):
//   rank0 -> front, rank1 -> back, rank2 -> front+1, rank3 -> back-1, ...
//
// Example (5 ranked items 0..4): [0, 2, 4, 3, 1]  — 0 & 1 (top two) at edges,
// 4 (least important) dead-center.
//
// Pure, header-only, allocation-light (one output vector), deterministic.
// No DB, no I/O. Templated on the value type so it works for slice strings,
// ids, or any movable/copyable element.

#include <vector>
#include <cstddef>

namespace icmg::core {

// Reorder `ranked` (sorted DESC by importance) into U-shaped attention order.
// Returns a new vector that is a permutation of the input. O(n) time, O(n)
// space, no element comparisons — placement is purely positional.
template <typename T>
std::vector<T> reorderUShaped(const std::vector<T>& ranked) {
    const std::size_t n = ranked.size();
    std::vector<T> out(ranked);  // copy so size is fixed; entries overwritten below
    if (n < 2) return out;       // empty / single: already U-shaped trivially

    std::size_t lo = 0;          // next front slot
    std::size_t hi = n - 1;      // next back slot
    for (std::size_t i = 0; i < n; ++i) {
        if ((i & 1u) == 0u) {    // even rank index -> front edge, inward
            out[lo++] = ranked[i];
        } else {                 // odd rank index -> back edge, inward
            out[hi--] = ranked[i];
        }
    }
    return out;
}

}  // namespace icmg::core
