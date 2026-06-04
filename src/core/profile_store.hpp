#pragma once
// Zoned profile/skill store. Entries keyed (user_id, zone, key) with a kind tag,
// in the exe-dir persona DB (cross-project shared). Table bootstrapped at first use.
// Content-neutral: stores whatever text the user supplies (work profiles, skills, notes).
#include "db.hpp"
#include <string>
#include <utility>
#include <vector>

namespace icmg::core {

struct ProfileRow {
    std::string zone, key, kind, content;
    long long updated_at = 0;
    std::string source = "unknown";   // provenance: who/what supplied this entry
};

class ProfileStore {
public:
    explicit ProfileStore(Db& db);
    void put(const std::string& user, const std::string& zone, const std::string& key,
             const std::string& kind, const std::string& content,
             const std::string& source = "unknown");
    bool get(const std::string& user, const std::string& zone, const std::string& key,
             std::string& content_out, std::string& kind_out);
    // Provenance overload: also returns source.
    bool get(const std::string& user, const std::string& zone, const std::string& key,
             std::string& content_out, std::string& kind_out, std::string& source_out);
    std::vector<ProfileRow> listZone(const std::string& user, const std::string& zone);
    std::vector<ProfileRow> search(const std::string& user, const std::string& query); // LIKE fallback
    void forget(const std::string& user, const std::string& zone, const std::string& key);
    // Distinct zones with entry counts for a user, busiest first.
    std::vector<std::pair<std::string,int>> zoneCounts(const std::string& user);
private:
    Db& db_;
    void ensure();
};

}  // namespace icmg::core
