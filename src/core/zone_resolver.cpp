#include "zone_resolver.hpp"
#include <algorithm>

namespace icmg::core {

namespace {
// Default seed rules: ordered most-specific first.
// Format: (glob, zone)
const std::vector<std::pair<std::string,std::string>> SEED_RULES = {
    {"src/api/**",     "api"},
    {"src/sync/**",    "sync"},
    {"src/ui/**",      "ui"},
    {"src/web/**",     "ui"},
    {"src/cli/**",     "cli"},
    {"src/mcp/**",     "mcp"},
    {"src/rtk/**",     "rtk"},
    {"src/icm/**",     "memory"},
    {"src/graph/**",   "graph"},
    {"src/rules/**",   "rules"},
    {"src/sp/**",      "sp"},
    {"src/import/**",  "import"},
    {"src/export/**",  "import"},
    {"src/data/**",    "data"},
    {"src/viz/**",     "viz"},
    {"src/core/**",    "core"},
    {"src/abbreviation/**", "abbreviation"},
    {"tests/**",       "tests"},
    {"migrations/**",  "schema"},
    {"docs/**",        "docs"},
};

// Normalize path separators to forward slash, strip leading "./" or ".\".
std::string normalize(const std::string& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.size() >= 2 && p[0] == '.' && p[1] == '/') p = p.substr(2);
    return p;
}
} // namespace

ZoneResolver::ZoneResolver(Db& db) : db_(db) {}

void ZoneResolver::ensureSeeded() {
    if (seeded_) return;
    seeded_ = true;

    // If zone_config table doesn't exist (e.g. legacy DB before migration 6 ran,
    // or unit-test schema without it), fall back to seed rules cached in-memory only.
    bool table_exists = false;
    try {
        db_.query("SELECT 1 FROM sqlite_master WHERE type='table' AND name='zone_config'", {},
                  [&](const Row& r) { if (!r.empty()) table_exists = true; });
    } catch (...) { /* leave table_exists=false */ }

    if (!table_exists) {
        // In-memory cache from seed rules — resolver still works, just not persistent.
        cache_.clear();
        for (auto& [glob, zone] : SEED_RULES) cache_.emplace_back(glob, zone);
        std::sort(cache_.begin(), cache_.end(),
                  [](auto& a, auto& b){ return a.first.size() > b.first.size(); });
        return;
    }

    // Seed only if zone_config has just the default entry (count <= 1).
    int existing = 0;
    try {
        db_.query("SELECT COUNT(*) FROM zone_config", {},
                  [&](const Row& r) { if (!r.empty()) try { existing = std::stoi(r[0]); } catch(...){} });
    } catch (...) {}
    if (existing <= 1) {
        for (auto& [glob, zone] : SEED_RULES) {
            try {
                db_.run(
                    "INSERT OR IGNORE INTO zone_config(zone, description, path_glob) "
                    "VALUES(?, ?, ?)",
                    {zone, "auto-seeded", glob});
            } catch (...) { /* skip if insert fails */ }
        }
    }
    loadCache();
}

void ZoneResolver::loadCache() {
    cache_.clear();
    db_.query("SELECT path_glob, zone FROM zone_config "
              "WHERE path_glob IS NOT NULL AND path_glob != '' "
              "ORDER BY length(path_glob) DESC",  // longer globs first = more specific
              {},
              [&](const Row& r) {
                  if (r.size() >= 2 && !r[0].empty()) cache_.emplace_back(r[0], r[1]);
              });
}

bool ZoneResolver::matchGlob(const std::string& glob, const std::string& path) {
    // Support: "**" suffix → prefix match; "*.<ext>" → extension match;
    // otherwise → substring match (anchored at any path component boundary).
    if (glob.size() >= 3 && glob.substr(glob.size() - 3) == "/**") {
        std::string prefix = glob.substr(0, glob.size() - 3);
        return path.size() > prefix.size() &&
               path.compare(0, prefix.size(), prefix) == 0 &&
               (path[prefix.size()] == '/' || prefix.empty());
    }
    if (glob.size() >= 2 && glob[0] == '*' && glob[1] == '.') {
        std::string ext = glob.substr(1);  // ".cs"
        return path.size() >= ext.size() &&
               path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
    }
    // Literal substring at component boundary
    return path.find(glob) != std::string::npos;
}

std::string ZoneResolver::resolveForPath(const std::string& path) {
    ensureSeeded();
    std::string p = normalize(path);
    for (auto& [glob, zone] : cache_) {
        if (matchGlob(glob, p)) return zone;
    }
    return "default";
}

void ZoneResolver::addRule(const std::string& zone, const std::string& glob,
                           const std::string& description) {
    db_.run(
        "INSERT INTO zone_config(zone, description, path_glob) VALUES(?, ?, ?) "
        "ON CONFLICT(zone) DO UPDATE SET path_glob=excluded.path_glob, "
        "description=COALESCE(excluded.description, zone_config.description)",
        {zone, description, glob});
    seeded_ = false;  // force cache reload
    ensureSeeded();
}

void ZoneResolver::removeZone(const std::string& zone) {
    db_.run("DELETE FROM zone_config WHERE zone = ?", {zone});
    seeded_ = false;
    ensureSeeded();
}

std::vector<std::pair<std::string,std::string>> ZoneResolver::listZones() {
    ensureSeeded();
    std::vector<std::pair<std::string,std::string>> out;
    db_.query("SELECT zone, COALESCE(path_glob, '') FROM zone_config ORDER BY zone", {},
              [&](const Row& r) {
                  if (r.size() >= 2) out.emplace_back(r[0], r[1]);
              });
    return out;
}

int ZoneResolver::rebuild() {
    ensureSeeded();
    int updated = 0;

    // Pull (id, path) for all graph nodes, recompute zone, update if changed.
    struct Row2 { int64_t id; std::string path; std::string zone; };
    std::vector<Row2> rows;
    db_.query("SELECT id, path, zone FROM graph_nodes", {},
              [&](const Row& r) {
                  if (r.size() < 3) return;
                  Row2 x;
                  try { x.id = std::stoll(r[0]); } catch(...) { return; }
                  x.path = r[1];
                  x.zone = r[2];
                  rows.push_back(std::move(x));
              });

    for (auto& r : rows) {
        std::string newzone = resolveForPath(r.path);
        if (newzone != r.zone) {
            db_.run("UPDATE graph_nodes SET zone = ? WHERE id = ?",
                    {newzone, std::to_string(r.id)});
            ++updated;
        }
    }
    return updated;
}

int ZoneResolver::assign(const std::string& glob, const std::string& zone) {
    int updated = 0;
    struct Row2 { int64_t id; std::string path; };
    std::vector<Row2> rows;
    db_.query("SELECT id, path FROM graph_nodes", {},
              [&](const Row& r) {
                  if (r.size() < 2) return;
                  Row2 x;
                  try { x.id = std::stoll(r[0]); } catch(...) { return; }
                  x.path = r[1];
                  rows.push_back(std::move(x));
              });
    for (auto& r : rows) {
        if (matchGlob(glob, normalize(r.path))) {
            db_.run("UPDATE graph_nodes SET zone = ? WHERE id = ?",
                    {zone, std::to_string(r.id)});
            ++updated;
        }
    }
    return updated;
}

} // namespace icmg::core
