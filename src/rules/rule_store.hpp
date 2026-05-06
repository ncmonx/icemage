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

private:
    core::Db& db_;

    static Rule fromRow(const core::Row& r);
};

} // namespace icmg::rules
