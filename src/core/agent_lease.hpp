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

// --- shared-file serialization + dedup (for the `icmg lock` command) ---------
// One TSV line: pid \t host \t heartbeat_at \t scope (scope last -> may contain
// any char but newline; no escaping needed).
inline std::string leaseToLine(const AgentLease& l) {
    return std::to_string(l.pid) + "\t" + l.host + "\t" +
           std::to_string(l.heartbeat_at) + "\t" + l.scope;
}
inline bool leaseFromLine(const std::string& line, AgentLease& out) {
    size_t t1 = line.find('\t'); if (t1 == std::string::npos) return false;
    size_t t2 = line.find('\t', t1 + 1); if (t2 == std::string::npos) return false;
    size_t t3 = line.find('\t', t2 + 1); if (t3 == std::string::npos) return false;
    try {
        out.pid          = std::stoll(line.substr(0, t1));
        out.heartbeat_at = std::stoll(line.substr(t2 + 1, t3 - t2 - 1));
    } catch (...) { return false; }
    out.host  = line.substr(t1 + 1, t2 - t1 - 1);
    out.scope = line.substr(t3 + 1);
    if (out.scope.empty()) return false;
    return true;
}
// Last appended lease per scope wins (a release tombstone with heartbeat_at=0,
// appended last, supersedes an earlier claim). Order-preserving by first scope.
inline std::vector<AgentLease> lastPerScope(const std::vector<AgentLease>& all) {
    std::vector<AgentLease> out;
    for (const auto& l : all) {
        bool found = false;
        for (auto& x : out) if (x.scope == l.scope) { x = l; found = true; break; }
        if (!found) out.push_back(l);
    }
    return out;
}

}  // namespace icmg::core
