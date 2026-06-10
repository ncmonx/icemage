#pragma once
// Reproducible read-savings benchmark math. `icmg bench savings` runs this on
// the user's OWN repo so the token-reduction claim is something a skeptic can
// reproduce: reading a file via `icmg context` caps the emitted body, so a large
// file costs ~cap tokens instead of its full size. Pure + unit-testable.
#include <vector>
#include <algorithm>

namespace icmg::cli {

struct ReadSavings {
    long long naiveTokens = 0;   // sum of full-file tokens (raw Read of all)
    long long icmgTokens  = 0;   // sum of min(file, cap) (icmg context excerpt)
    int       pctSaved    = 0;
    int       files       = 0;
};

// fileTokens = per-file full token counts; capTokens = the icmg context body cap.
inline ReadSavings benchReadSavings(const std::vector<long long>& fileTokens,
                                    long long capTokens) {
    ReadSavings r;
    r.files = (int)fileTokens.size();
    for (long long t : fileTokens) {
        if (t < 0) t = 0;
        r.naiveTokens += t;
        r.icmgTokens  += (t < capTokens) ? t : capTokens;
    }
    if (r.naiveTokens > 0)
        r.pctSaved = (int)((r.naiveTokens - r.icmgTokens) * 100 / r.naiveTokens);
    return r;
}

}  // namespace icmg::cli
