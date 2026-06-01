// v2.0.0 Phase 4 TDD: multi-agent conflict-free lease resolver. Verifies the
// PURE decision (no DB): grant unless a live lease on the same scope is held by
// a different owner; stale leases are reclaimable; re-claiming own scope is ok.

#include "../test_main.hpp"
#include "../../src/core/agent_lease.hpp"

#include <string>
#include <vector>

using namespace icmg::core;

namespace {
const int64_t NOW   = 1'000'000;
const int64_t STALE = 60;  // stale after 60s without heartbeat
AgentLease lease(const std::string& scope, int64_t pid, const std::string& host, int64_t hb) {
    AgentLease l; l.scope = scope; l.pid = pid; l.host = host; l.heartbeat_at = hb; return l;
}
}  // namespace

TEST("lease: empty table grants") {
    auto r = resolveClaim({}, "src/core", 100, "boxA", NOW, STALE);
    ASSERT_TRUE(r.granted);
}

TEST("lease: live other owner denies + reports conflict") {
    std::vector<AgentLease> ex{ lease("src/core", 200, "boxB", NOW - 10) };  // fresh
    auto r = resolveClaim(ex, "src/core", 100, "boxA", NOW, STALE);
    ASSERT_FALSE(r.granted);
    ASSERT_EQ((int)r.conflict_pid, 200);
    ASSERT_EQ(r.conflict_host, std::string("boxB"));
}

TEST("lease: stale other owner is reclaimable") {
    std::vector<AgentLease> ex{ lease("src/core", 200, "boxB", NOW - 120) };  // stale > 60
    auto r = resolveClaim(ex, "src/core", 100, "boxA", NOW, STALE);
    ASSERT_TRUE(r.granted);
}

TEST("lease: re-claiming own scope is idempotent grant") {
    std::vector<AgentLease> ex{ lease("src/core", 100, "boxA", NOW - 5) };
    auto r = resolveClaim(ex, "src/core", 100, "boxA", NOW, STALE);
    ASSERT_TRUE(r.granted);
}

TEST("lease: different scope never conflicts") {
    std::vector<AgentLease> ex{ lease("src/mcp", 200, "boxB", NOW) };
    auto r = resolveClaim(ex, "src/core", 100, "boxA", NOW, STALE);
    ASSERT_TRUE(r.granted);
}

TEST("lease: boundary — exactly stale_after is still live") {
    std::vector<AgentLease> ex{ lease("z", 200, "boxB", NOW - STALE) };  // now-hb == STALE, not > STALE
    auto r = resolveClaim(ex, "z", 100, "boxA", NOW, STALE);
    ASSERT_FALSE(r.granted);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
