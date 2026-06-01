#pragma once
#include "db.hpp"
#include <string>
#include <vector>
#include <utility>

namespace icmg::core {

// ZoneResolver maps file paths to zone names via glob rules stored in zone_config.
// Auto-seeds common rules on first use (api/sync/ui/tests/schema). Manual overrides
// via addRule(); falls back to "default" when no rule matches.
//
// Globs supported: prefix glob with "**" suffix (e.g., "src/api/**"),
// extension glob "*.<ext>", or literal substring match.
class ZoneResolver {
public:
    explicit ZoneResolver(Db& db);

    // Returns zone name for the given path; "default" if no rule matches.
    std::string resolveForPath(const std::string& path);

    // Add a path glob → zone mapping. Persists to zone_config (path_glob column).
    // Note: zone_config has zone as primary key, so a single zone can have multiple
    // globs only via JSON list in path_glob (kept simple: one glob per row → if you
    // need multiple, create distinct logical zones or extend the schema).
    void addRule(const std::string& zone, const std::string& glob,
                 const std::string& description = "");

    // Remove zone (and its rule). Existing nodes keep the value until rebuild.
    void removeZone(const std::string& zone);

    // List all configured zones. Pair: (zone, glob).
    std::vector<std::pair<std::string,std::string>> listZones();

    // Re-tag every node from current rules. Returns count of rows updated.
    int rebuild();

    // Bulk re-tag rows whose path matches glob.
    int assign(const std::string& glob, const std::string& zone);

private:
    Db& db_;
    bool seeded_ = false;
    std::vector<std::pair<std::string,std::string>> cache_;  // (glob, zone), most-specific first

    void ensureSeeded();
    void loadCache();
    static bool matchGlob(const std::string& glob, const std::string& path);
};

} // namespace icmg::core
