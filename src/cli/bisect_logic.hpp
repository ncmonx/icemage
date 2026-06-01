#pragma once
// v1.79 M5 smart-bisect: pure bisect core (no git, no I/O — testable).
// Commit list is ordered oldest..newest. commits[0] assumed GOOD,
// commits[n-1] assumed BAD. firstBadIndex binary-searches the boundary:
// the lowest index i where isBad(i) is true. isBad is the test predicate
// (true = test fails at that commit). Returns -1 if none bad.
#include <functional>
#include <vector>
#include <cstddef>

namespace icmg::cli {

// Number of test runs a binary search will need over n candidates (the
// interior commits between known-good[0] and known-bad[n-1]). Used for the
// "this will run ~K tests" estimate. n = total commits incl endpoints.
inline int bisectStepsEstimate(int n) {
    if (n <= 2) return 0;                 // only endpoints, nothing to test
    int interior = n - 2, steps = 0;
    long long cap = 1;                    // smallest steps with 2^steps >= interior+1
    while (cap < (long long)interior + 1) { cap <<= 1; ++steps; }
    return steps;
}

// Binary search for the first BAD index in [0, n). isBad(i) memoized by caller
// if expensive. Assumes monotonic: once bad, stays bad (git history property).
// Returns the lowest i with isBad(i)==true, or -1 if no index is bad.
inline int firstBadIndex(int n, const std::function<bool(int)>& isBad) {
    int lo = 0, hi = n;                   // search in [lo, hi)
    int ans = -1;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (isBad(mid)) { ans = mid; hi = mid; }
        else            { lo = mid + 1; }
    }
    return ans;
}

} // namespace icmg::cli
