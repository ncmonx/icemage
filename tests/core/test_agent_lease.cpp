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

// --- lock #3: lease serialize + last-per-scope (for `icmg lock` shared file) ---
TEST("lease: line round-trips (scope path with spaces)") {
    AgentLease l; l.pid = 42; l.host = "box1"; l.heartbeat_at = 1717; l.scope = "src/my file.cpp";
    AgentLease got;
    ASSERT_TRUE(leaseFromLine(leaseToLine(l), got));
    ASSERT_EQ(got.pid, (int64_t)42);
    ASSERT_EQ(got.host, std::string("box1"));
    ASSERT_EQ(got.heartbeat_at, (int64_t)1717);
    ASSERT_EQ(got.scope, std::string("src/my file.cpp"));
}

TEST("lease: malformed line -> false") {
    AgentLease got;
    ASSERT_TRUE(!leaseFromLine("", got));
    ASSERT_TRUE(!leaseFromLine("1\thost\t10", got));        // missing scope field
    ASSERT_TRUE(!leaseFromLine("x\thost\t10\ts", got));     // non-numeric pid
    ASSERT_TRUE(!leaseFromLine("1\thost\t10\t", got));      // empty scope
}

TEST("lease: lastPerScope - release tombstone supersedes earlier claim") {
    std::vector<AgentLease> log;
    AgentLease claim;  claim.pid = 1; claim.host = "h"; claim.heartbeat_at = 100; claim.scope = "a.cpp";
    AgentLease other;  other.pid = 2; other.host = "h"; other.heartbeat_at = 100; other.scope = "b.cpp";
    AgentLease release; release.pid = 1; release.host = "h"; release.heartbeat_at = 0; release.scope = "a.cpp"; // tombstone
    log = {claim, other, release};
    auto last = lastPerScope(log);
    ASSERT_EQ(last.size(), (size_t)2);
    for (auto& l : last) if (l.scope == "a.cpp") ASSERT_EQ(l.heartbeat_at, (int64_t)0);  // tombstone won
}
