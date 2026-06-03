#pragma once
// Greedy clustering of similar prompts (word-set Jaccard) — the core of
// `icmg profile qa-frequent`: surface groups of recurring/near-duplicate prompts
// from the auto-recorded prompt_history as candidates to promote into a saved
// reusable skill (v2 roadmap pillar 4: auto-suggest skills from frequent patterns).
// Pure + model-free (reuses promptJaccard); read-only over the history.
#include "prompt_history.hpp"   // promptJaccard
#include <algorithm>
#include <string>
#include <vector>

namespace icmg::core {

struct PromptCluster { std::string representative; std::vector<std::string> members; };

// Greedy single-pass clustering: each prompt joins the first existing cluster whose
// representative scores >= threshold against it, else seeds a new cluster. Clusters
// are returned largest-first (stable for equal sizes), so the most frequent pattern
// leads. Deterministic; O(n * clusters).
inline std::vector<PromptCluster> clusterSimilar(const std::vector<std::string>& prompts,
                                                 double threshold) {
    std::vector<PromptCluster> clusters;
    for (const auto& p : prompts) {
        bool placed = false;
        for (auto& c : clusters) {
            if (promptJaccard(c.representative, p) >= threshold) {
                c.members.push_back(p);
                placed = true;
                break;
            }
        }
        if (!placed) clusters.push_back({p, {p}});
    }
    std::stable_sort(clusters.begin(), clusters.end(),
                     [](const PromptCluster& a, const PromptCluster& b) {
                         return a.members.size() > b.members.size();
                     });
    return clusters;
}

}  // namespace icmg::core
