#pragma once
#include <string>
#include <cstdint>
#include <stdexcept>

namespace icmg::rules {

struct Rule {
    int64_t     id         = 0;
    std::string scope_path;   // "/" = root, "src/" = src subtree
    std::string rule_type;    // coding|arch|workflow|model|custom
    std::string name;
    std::string content;
    int         priority   = 0;
    bool        active     = true;
    int64_t     created_at      = 0;
    // Trial / supersession fields (migration 0026)
    int64_t     supersedes_id   = 0;   // 0 = none; points to rule being superseded
    int         trial_mode      = 0;   // 0 = confirmed, 1 = in trial
    int         trial_prompts   = 0;   // prompts since rule became active
    int         trial_threshold = 5;   // auto-confirm after this many quiet prompts
};

// Thrown when add() violates UNIQUE(scope_path, rule_type, name)
struct RuleConflictError : std::runtime_error {
    explicit RuleConflictError(const std::string& msg)
        : std::runtime_error(msg) {}
};

} // namespace icmg::rules
