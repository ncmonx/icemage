#pragma once
// Zoned profile/skill store. Entries keyed (user_id, zone, key) with a kind tag,
// in the exe-dir persona DB (cross-project shared). Table bootstrapped at first use.
// Content-neutral: stores whatever text the user supplies (work profiles, skills, notes).
#include "db.hpp"
#include <string>
#include <vector>

namespace icmg::core {

struct ProfileRow {
    std::string zone, key, kind, content;
    long long updated_at = 0;
};

class ProfileStore {
public:
    explicit ProfileStore(Db& db);
    void put(const std::string& user, const std::string& zone, const std::string& key,
             const std::string& kind, const std::string& content);
    bool get(const std::string& user, const std::string& zone, const std::string& key,
             std::string& content_out, std::string& kind_out);
    std::vector<ProfileRow> listZone(const std::string& user, const std::string& zone);
    std::vector<ProfileRow> search(const std::string& user, const std::string& query); // LIKE fallback
    void forget(const std::string& user, const std::string& zone, const std::string& key);
private:
    Db& db_;
    void ensure();
};

}  // namespace icmg::core
