// TDD (2026-11-11): ACT-R base-level activation as an opt-in recency/frequency
// formula for memory ranking (ICMG_RECENCY=actr). Research:
// docs/plans/2026-11-11-token-memory-research.md item 5.
#include "../test_main.hpp"
#include "../../src/imem/actr_activation.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/imem/scorer.hpp"

#include <cmath>
#include <cstdlib>

using namespace icmg::imem;

static void setRecencyEnv(const char* v) {
#ifdef _WIN32
    _putenv_s(kActrRecencyEnv, v ? v : "");
#else
    if (v) setenv(kActrRecencyEnv, v, 1); else unsetenv(kActrRecencyEnv);
#endif
}

TEST("actr: single use is the pure power law ln(t^-d)") {
    ASSERT_TRUE(std::fabs(actrBaseLevel(1, 4.0, 4.0) - std::log(0.5)) < 1e-9);   // 4^-0.5
}

TEST("actr: more uses -> higher activation; older last use -> lower") {
    const double one = actrBaseLevel(1, 24, 720);
    const double ten = actrBaseLevel(10, 24, 720);
    ASSERT_TRUE(ten > one);
    ASSERT_TRUE(actrBaseLevel(5, 24, 720) > actrBaseLevel(5, 240, 720));
}

TEST("actr: power law decays slower than the raw exp(-0.01h) term at a month") {
    const double actrMonth = actrRecencyMult(1, 720.0, 720.0);
    const double expMonth  = std::exp(-0.01 * 720.0);
    ASSERT_TRUE(actrMonth > 0.02);
    ASSERT_TRUE(actrMonth > 20 * expMonth);
    ASSERT_TRUE(actrRecencyMult(1, 1.0, 1.0) > actrMonth);   // still monotone
    ASSERT_TRUE(actrRecencyMult(1, 1.0, 1.0) < 1.0);
}

TEST("actr: degenerate inputs are safe (age 0, lifetime < age, n <= 0)") {
    ASSERT_TRUE(std::isfinite(actrBaseLevel(1, 0.0, 0.0)));
    ASSERT_TRUE(std::isfinite(actrBaseLevel(3, 10.0, 5.0)));
    ASSERT_TRUE(std::isfinite(actrBaseLevel(0, 10.0, 50.0)));
    const double m = actrRecencyMult(-2, -5.0, -1.0);
    ASSERT_TRUE(m > 0.0 && m < 1.0);
}

TEST("actr: ICMG_RECENCY=actr switches Scorer recency to the ACT-R multiplier") {
    MemoryNode n;
    n.content = "deploy pipeline notes";
    n.topic = "ops";
    n.frequency = 4;
    const int64_t now = actrNowSec();
    n.last_used  = now - 2 * 86400;     // last use 2 days ago
    n.created_at = now - 30 * 86400;    // created 30 days ago
    auto& sc = Scorer::instance();
    setRecencyEnv(nullptr);
    ASSERT_FALSE(actrRecencyFromEnv());
    auto legacy = sc.scoreDetailed("deploy", n);
    ASSERT_TRUE(legacy.freq > 1.0);                       // log(2 + 4)
    setRecencyEnv("actr");
    ASSERT_TRUE(actrRecencyFromEnv());
    auto a = sc.scoreDetailed("deploy", n);
    setRecencyEnv(nullptr);
    const double want = actrRecencyMult(4, 48.0, 720.0);
    ASSERT_TRUE(std::fabs(a.recency - want) < 1e-3);      // frequency folded in
    ASSERT_TRUE(std::fabs(a.freq - 1.0) < 1e-12);         // so no extra log(2+f)
    MemoryNode never = n;
    never.last_used = 0;
    never.created_at = 0;
    setRecencyEnv("actr");
    const double unk = sc.scoreDetailed("deploy", never).recency;
    setRecencyEnv(nullptr);
    ASSERT_TRUE(std::fabs(unk - kActrUnknownMult) < 1e-12);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
