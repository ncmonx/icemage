#pragma once
// Command recommender core: rank known commands against a natural-language intent.
// Pure + model-free -- reuses promptJaccard (word-set similarity) so the long tail
// of rarely-remembered commands becomes discoverable via `icmg suggest "<intent>"`.
// The CLI layer feeds in the live registry (name + description); this header has no
// registry dependency so it is unit-testable in isolation.
#include "prompt_history.hpp"   // promptJaccard
#include <algorithm>
#include <string>
#include <vector>

namespace icmg::core {

struct CmdDoc { std::string name, desc; };
struct CmdHit { std::string name; double score = 0.0; };

// Score each doc by promptJaccard(intent, "name desc"), sort by score descending,
// drop zero-overlap docs, and return at most topN hits. Ties keep input order
// (stable_sort), so the result is deterministic.
inline std::vector<CmdHit> rankCommands(const std::string& intent,
                                        const std::vector<CmdDoc>& docs, int topN) {
    std::vector<CmdHit> hits;
    hits.reserve(docs.size());
    for (const auto& d : docs) {
        double s = promptJaccard(intent, d.name + " " + d.desc);
        if (s > 0.0) hits.push_back({d.name, s});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const CmdHit& a, const CmdHit& b) { return a.score > b.score; });
    if (topN >= 0 && static_cast<int>(hits.size()) > topN) hits.resize(topN);
    return hits;
}

}  // namespace icmg::core
