#include "rule_resolver.hpp"
#include <algorithm>
#include <unordered_map>

namespace icmg::rules {

RuleResolver::RuleResolver(RuleStore& store) : store_(store) {}

std::vector<Rule> RuleResolver::resolve(const std::string& file_path) const {
    // forPath already returns sorted by scope_path length (root first)
    auto rules = store_.forPath(file_path);

    // Within same scope_path: sort by priority desc, then id asc
    // forPath orders by length(scope_path),priority,id — already correct.
    return rules;
}

std::vector<std::pair<Rule, Rule>> RuleResolver::conflicts(
    const std::string& file_path) const {

    auto rules = store_.forPath(file_path);

    // Group by (scope_path, rule_type, name)
    // Key: scope+":"+type+":"+name
    std::unordered_map<std::string, std::vector<Rule>> groups;
    for (auto& r : rules) {
        std::string key = r.scope_path + ":" + r.rule_type + ":" + r.name;
        groups[key].push_back(r);
    }

    std::vector<std::pair<Rule, Rule>> result;
    for (auto& [key, group] : groups) {
        if (group.size() < 2) continue;
        // Sort: winner first (higher priority, or lower id on tie)
        std::sort(group.begin(), group.end(), [](const Rule& a, const Rule& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.id < b.id;
        });
        // Emit (winner, loser) pairs
        for (size_t i = 1; i < group.size(); ++i)
            result.emplace_back(group[0], group[i]);
    }
    return result;
}

} // namespace icmg::rules
