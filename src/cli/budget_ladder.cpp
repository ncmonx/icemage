// 2026-11-11: see budget_ladder.hpp.
#include "budget_ladder.hpp"

#include <algorithm>
#include <cstring>

namespace icmg::cli {

namespace {
std::string sigLine(const LadderSig& s) {
    return "  L" + std::to_string(s.line) + "  " + s.text + "\n";
}

int countLines(const std::string& body) {
    int n = (int)std::count(body.begin(), body.end(), '\n');
    if (!body.empty() && body.back() != '\n') ++n;
    return n;
}
}  // namespace

LadderResult fitBodyToBudget(const std::string& body, const std::vector<LadderSig>& sigsIn,
                             size_t budget) {
    LadderResult r;
    if (body.size() <= budget) {
        r.text = body;
        r.rung = "full";
        r.keptLines = countLines(body);
        return r;
    }
    // Line end offsets (exclusive): prefix of P lines = body[0, ends[P-1]).
    std::vector<size_t> ends;
    for (size_t i = 0; i < body.size(); ++i)
        if (body[i] == '\n') ends.push_back(i + 1);
    if (!body.empty() && body.back() != '\n') ends.push_back(body.size());
    auto prefixBytes = [&](size_t p) { return p == 0 ? size_t(0) : ends[p - 1]; };

    std::vector<LadderSig> sigs = sigsIn;
    std::stable_sort(sigs.begin(), sigs.end(),
                     [](const LadderSig& a, const LadderSig& b) { return a.line < b.line; });
    if (!sigs.empty()) {
        // suffix[i] = bytes of signature lines i..end
        std::vector<size_t> suffix(sigs.size() + 1, 0);
        for (size_t i = sigs.size(); i-- > 0;) suffix[i] = suffix[i + 1] + sigLine(sigs[i]).size();
        const size_t hdr = std::strlen(kLadderSkelHeader);
        long best = -1;
        size_t si = 0;   // first sig with line > p
        for (size_t p = 0; p <= ends.size(); ++p) {
            while (si < sigs.size() && sigs[si].line <= (int)p) ++si;
            if (si == sigs.size()) break;   // nothing left to skeletonize
            if (prefixBytes(p) + hdr + suffix[si] <= budget) best = (long)p;
            else if (prefixBytes(p) > budget) break;
        }
        if (best >= 0) {
            const size_t p = (size_t)best;
            r.text = body.substr(0, prefixBytes(p)) + kLadderSkelHeader;
            for (const auto& s : sigs)
                if (s.line > (int)p) r.text += sigLine(s);
            r.rung = p == 0 ? "skeleton" : "prefix+skeleton";
            r.keptLines = (int)p;
            return r;
        }
    }
    size_t p = 0;
    while (p < ends.size() && ends[p] <= budget) ++p;
    r.text = body.substr(0, prefixBytes(p));
    r.rung = "truncate";
    r.keptLines = (int)p;
    return r;
}

}  // namespace icmg::cli
