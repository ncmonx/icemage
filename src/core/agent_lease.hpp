#pragma once
// v2.0.0 Phase 4: multi-agent DB — conflict-free work leases. Multiple agents
// share one DB; a lease claims a work SCOPE (file / zone / task id) so two
// agents don't clobber the same area. Pure conflict resolver (no DB) — the
// command layer reads existing leases from the agent_leases table and applies
// this to decide grant/deny. A lease is reclaimable once its heartbeat goes
// stale (owning process died without releasing).
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::core {

struct AgentLease {
    std::string scope;
    int64_t     pid          = 0;
    std::string host;
    int64_t     heartbeat_at = 0;   // unix seconds of last heartbeat
};

struct ClaimResult {
    bool        granted = false;
    std::string conflict_host;       // owner of the blocking lease (empty if granted)
    int64_t     conflict_pid = 0;
};

// Decide whether (my_pid@my_host) may claim `scope`.
// Grant unless a NON-stale lease on the SAME scope is held by a DIFFERENT owner.
//   - same owner re-claiming its own scope  → granted (idempotent refresh)
//   - existing lease older than stale_after  → ignored (dead owner, reclaimable)
//   - live lease by another owner            → denied, with conflict owner info
inline ClaimResult resolveClaim(const std::vector<AgentLease>& existing,
                                const std::string& scope,
                                int64_t my_pid, const std::string& my_host,
                                int64_t now, int64_t stale_after_sec) {
    for (const auto& l : existing) {
        if (l.scope != scope) continue;
        const bool same_owner = (l.pid == my_pid && l.host == my_host);
        if (same_owner) continue;
        if (now - l.heartbeat_at > stale_after_sec) continue;  // stale → reclaimable
        return ClaimResult{false, l.host, l.pid};              // live other owner
    }
    return ClaimResult{true, "", 0};
}

}  // namespace icmg::core
