// 2026-09-07 token-killer/memory-optimizer A: deep-forget (unlearning
// propagation, arXiv 2609.04875 "Forgetting Without Restarting"). Forgetting a
// memory node soft-deletes ONE row, but its content typically leaked into
// derived artifacts: session snapshots, wflog entries (both live in
// memory_nodes under their own topics), consolidated quick notes, compact
// handoff files. `memory forget <id> --deep` surfaces that residue so it can
// be purged/redacted. Pure functions -- reuses the contradiction_scan
// tokenizer; the cmd layer feeds candidates and applies actions.
#pragma once
#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "contradiction_scan.hpp"   // tokens(), jaccard(), lower()

namespace icmg::imem {

struct ResidueCandidate {
    int64_t     id = 0;        // node id (0 for file artifacts)
    std::string source;        // topic, or file path for file artifacts
    std::string text;
};

struct ResidueHit {
    int64_t     id = 0;
    std::string source;
    double      overlap = 0.0; // containment score vs forgotten text
};

// Directional containment: how much of the FORGOTTEN text's token set lives
// inside the candidate? (Jaccard is wrong here: a long snapshot containing the
// whole secret verbatim would score low symmetric overlap.)
inline double containment(const std::set<std::string>& forgotten,
                          const std::set<std::string>& candidate) {
    if (forgotten.empty()) return 0.0;
    size_t inter = 0;
    for (const auto& t : forgotten)
        if (candidate.count(t)) ++inter;
    return (double)inter / (double)forgotten.size();
}

// Rank derived artifacts still carrying the forgotten content. min_containment
// defaults strict-ish (0.5): at least half the forgotten tokens must appear.
// Tiny forgotten texts (<3 tokens) match everything -- require more tokens.
inline std::vector<ResidueHit> findResidue(const std::string& forgotten_text,
                                           const std::vector<ResidueCandidate>& candidates,
                                           double min_containment = 0.5,
                                           int max_out = 25) {
    using contra_detail::lower;
    using contra_detail::tokens;
    std::vector<ResidueHit> out;
    auto ftoks = tokens(lower(forgotten_text));
    if (ftoks.size() < 3) return out;   // too small to attribute reliably
    for (const auto& c : candidates) {
        double score = containment(ftoks, tokens(lower(c.text)));
        if (score < min_containment) continue;
        out.push_back({c.id, c.source, score});
    }
    std::sort(out.begin(), out.end(), [](const ResidueHit& a, const ResidueHit& b) {
        if (a.overlap != b.overlap) return a.overlap > b.overlap;
        return a.id < b.id;
    });
    if ((int)out.size() > max_out) out.resize(max_out);
    return out;
}

} // namespace icmg::imem
