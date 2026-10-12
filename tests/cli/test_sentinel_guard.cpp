// TDD (2026-10-12): sentinel IO-loop fix — guard logic.
//
// Bug A: reactions that never clear their condition (disk prune vs 1.5GB of
//        kept snapshots) re-fired every 15m forever = standby disk IO loop.
// Bug B: multiple live sessions ran sentinel 2-3x within the same minute
//        (audit.log RUN storm) — no cross-session run gate existed.
//
// sentinel_guard.hpp is pure (no fs/clock) so these tests are deterministic.

#include "../test_main.hpp"
#include "../../src/cli/sentinel_guard.hpp"

using namespace icmg::cli::sentinel;

// ---- parse / serialize ----------------------------------------------------

TEST("sentinel guard: parse empty input gives defaults") {
    GuardState st = parseGuardState("");
    ASSERT_EQ(st.last_run_ts, 0ll);
    ASSERT_TRUE(st.ineffective.empty());
}

TEST("sentinel guard: parse garbage input gives defaults") {
    GuardState st = parseGuardState("not json at all {{{");
    ASSERT_EQ(st.last_run_ts, 0ll);
    ASSERT_TRUE(st.ineffective.empty());
}

TEST("sentinel guard: serialize/parse round-trip") {
    GuardState st;
    st.last_run_ts = 1791768731;
    st.ineffective["disk"] = 3;
    st.ineffective["mirror"] = 1;
    GuardState back = parseGuardState(serializeGuardState(st));
    ASSERT_EQ(back.last_run_ts, 1791768731ll);
    ASSERT_EQ(back.ineffective["disk"], 3);
    ASSERT_EQ(back.ineffective["mirror"], 1);
}

TEST("sentinel guard: serialize drops healed (zero) checks") {
    GuardState st;
    st.last_run_ts = 100;
    st.ineffective["disk"] = 0;
    std::string s = serializeGuardState(st);
    ASSERT_TRUE(s.find("disk") == std::string::npos);
}

// ---- run dedup gate -------------------------------------------------------

TEST("sentinel guard: skip run when previous run too recent") {
    ASSERT_TRUE(shouldSkipRun(1000, 1100, 300));   // 100s < 300s gap
}

TEST("sentinel guard: allow run after interval passes") {
    ASSERT_TRUE(!shouldSkipRun(1000, 1301, 300));  // 301s >= 300s
}

TEST("sentinel guard: never skip on first run (ts=0)") {
    ASSERT_TRUE(!shouldSkipRun(0, 1100, 300));
}

TEST("sentinel guard: clock going backwards does not wedge the gate") {
    ASSERT_TRUE(!shouldSkipRun(2000, 1000, 300));
}

// ---- reaction-effectiveness suppression -----------------------------------

TEST("sentinel guard: fresh check is not suppressed") {
    GuardState st;
    ASSERT_TRUE(!shouldSuppress(st, "disk"));
}

TEST("sentinel guard: below max ineffective is not suppressed") {
    GuardState st;
    st.ineffective["disk"] = 1;
    ASSERT_TRUE(!shouldSuppress(st, "disk"));
}

TEST("sentinel guard: at max ineffective is suppressed") {
    GuardState st;
    st.ineffective["disk"] = kMaxIneffective;
    ASSERT_TRUE(shouldSuppress(st, "disk"));
}

TEST("sentinel guard: ineffective reaction increments count") {
    GuardState st;
    ASSERT_EQ(recordOutcome(st, "disk", /*still_warn=*/true), 1);
    ASSERT_EQ(recordOutcome(st, "disk", true), 2);
    ASSERT_TRUE(shouldSuppress(st, "disk"));
}

TEST("sentinel guard: effective reaction resets count") {
    GuardState st;
    st.ineffective["disk"] = 2;
    ASSERT_EQ(recordOutcome(st, "disk", /*still_warn=*/false), 0);
    ASSERT_TRUE(!shouldSuppress(st, "disk"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
