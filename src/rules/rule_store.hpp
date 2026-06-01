#pragma once
#include "rule.hpp"
#include "../core/db.hpp"
#include <optional>
#include <vector>
#include <string>

namespace icmg::rules {

class RuleStore {
public:
    explicit RuleStore(core::Db& db);

    // Returns new id. Throws RuleConflictError on UNIQUE violation.
    // Pass update=true to upsert (update existing active rule).
    int64_t add(const Rule& rule, bool update = false);

    bool remove(int64_t id);           // hard delete
    bool setActive(int64_t id, bool active);

    // All rules (active + inactive), ordered by scope_path then priority
    std::vector<Rule> all() const;

    // Rules whose scope_path is a prefix of path, active only.
    // Sorted: root (short scope) first, specific (long scope) last.
    std::vector<Rule> forPath(const std::string& path) const;

    // Lookup by id
    std::optional<Rule> get(int64_t id) const;

    // ---- trial / supersession -----------------------------------------------

    // Mark new_id as superseding old_id; set trial_mode=1, lower old priority.
    // Returns false if either id not found.
    bool supersede(int64_t new_id, int64_t old_id, int trial_threshold = 5);

    // Increment trial_prompts for all trial rules.
    // Auto-deletes superseded rule + confirms new rule when threshold reached.
    // Returns count of auto-confirmed pairs.
    int  trialTick();

    // Delete new (trial) rule, restore superseded rule's priority.
    bool revert(int64_t new_rule_id);

    // List all rules in trial mode.
    std::vector<Rule> trials() const;

private:
    core::Db& db_;

    static Rule fromRow(const core::Row& r);
};

} // namespace icmg::rules
