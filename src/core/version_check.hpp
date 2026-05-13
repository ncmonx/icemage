// Phase 85 Plan C: startup version-staleness check.
// Warn-only approach — never hard-blocks critical workflows.
// Lag tiers: 0-2=silent, 3-4=warn, 5-9=prominent, 10+=block init/graph-update.
#pragma once
#include <string>

namespace icmg::core {

struct VersionStatus {
    std::string current;
    std::string latest;
    int  lag;          // estimated semver steps behind (patch+minor combined)
    bool online;       // false = offline / fetch failed
    bool from_cache;   // true = served from 24h cache
};

// Check version staleness. 24h cache prevents network hit every startup.
// Cache file: <user_home>/.icmg_version_cache (JSON).
// If offline and cache >7d stale, lag=-1 and online=false.
VersionStatus checkVersionStaleness(const std::string& current_version,
                                    const std::string& repo = "ncmonx/icm-graph");

// Print warning to stderr based on lag. Silent for lag 0-2. No-op when !online.
void printVersionWarning(const VersionStatus& status);

// Returns true if lag >= 10 and the named command should be soft-blocked.
// Soft-blocked commands: "init", "graph" (update). Recall/search always run.
bool isCommandSoftBlocked(const std::string& cmd, const VersionStatus& status);

} // namespace icmg::core
