#pragma once
#include "rule_store.hpp"
#include <vector>
#include <utility>

namespace icmg::rules {

class RuleResolver {
public:
    explicit RuleResolver(RuleStore& store);

    // All active rules for file_path, ordered: root first, specific last.
    // Within same scope: sorted by priority desc, then id asc.
    std::vector<Rule> resolve(const std::string& file_path) const;

    // Conflicts: same scope+type+name → more than one active rule at same level.
    // Returns pairs (winner, loser) — winner = higher priority or lower id.
    std::vector<std::pair<Rule, Rule>> conflicts(const std::string& file_path) const;

private:
    RuleStore& store_;
};

} // namespace icmg::rules
