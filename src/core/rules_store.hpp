#pragma once
#include "db.hpp"
#include <string>
#include <vector>

namespace icmg::core {

struct RuleEntry {
    int64_t     id         = 0;
    std::string path;
    std::string content;
    std::string tag;
    bool        active     = true;
    int64_t     updated_at = 0;
};

class RulesStore {
public:
    explicit RulesStore(Db& db);

    // Insert or update by path (idempotent). Returns row id.
    int64_t upsert(const std::string& path, const std::string& content,
                   const std::string& tag = "");

    // Toggle active flag.
    bool setActive(const std::string& path, bool active);

    // List all rules.
    std::vector<RuleEntry> list() const;

    // List only active rules (used by inject).
    std::vector<RuleEntry> listActive() const;

private:
    Db& db_;
    static RuleEntry fromRow(const Row& r);
};

} // namespace icmg::core
