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
    int64_t     created_at = 0;
};

// Thrown when add() violates UNIQUE(scope_path, rule_type, name)
struct RuleConflictError : std::runtime_error {
    explicit RuleConflictError(const std::string& msg)
        : std::runtime_error(msg) {}
};

} // namespace icmg::rules
