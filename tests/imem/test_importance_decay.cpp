// v1.23.0 (TDD catchup): tier-aware ageDecay tests for v1.21.9 M2 feature.
//
// Verifies:
//   - critical (importance=3) -> frozen (decay multiplier 0 -> returns 1.0)
//   - high (2)                -> half rate (~180d half-life)
//   - medium (1)              -> baseline (~90d half-life)
//   - low (0)                 -> double rate (~45d half-life)
// Also covers the long-form name acceptance in importanceFromName().

#include "../test_main.hpp"
#include "../../src/imem/scorer.hpp"
#include "../../src/imem/memory_node.hpp"

#include <chrono>
#include <cmath>

using namespace icmg::imem;

static int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ---- importanceFromName: long forms accepted -----------------------------

TEST("importanceFromName: legacy short forms still work") {
    ASSERT_EQ(importanceFromName("low"),  0);
    ASSERT_EQ(importanceFromName("med"),  1);
    ASSERT_EQ(importanceFromName("high"), 2);
    ASSERT_EQ(importanceFromName("crit"), 3);
}

TEST("importanceFromName: long forms map to same ints") {
    ASSERT_EQ(importanceFromName("medium"),   1);
    ASSERT_EQ(importanceFromName("critical"), 3);
}

TEST("importanceFromName: unknown defaults to med") {
    ASSERT_EQ(importanceFromName(""),         1);
    ASSERT_EQ(importanceFromName("garbage"),  1);
}

// ---- importanceDecayMultiplier table -------------------------------------

TEST("importanceDecayMultiplier: critical -> 0 (frozen)") {
    ASSERT_TRUE(std::fabs(importanceDecayMultiplier(3) - 0.0) < 1e-9);
}

TEST("importanceDecayMultiplier: high -> 0.5") {
    ASSERT_TRUE(std::fabs(importanceDecayMultiplier(2) - 0.5) < 1e-9);
}

TEST("importanceDecayMultiplier: medium -> 1.0") {
    ASSERT_TRUE(std::fabs(importanceDecayMultiplier(1) - 1.0) < 1e-9);
}

TEST("importanceDecayMultiplier: low -> 2.0") {
    ASSERT_TRUE(std::fabs(importanceDecayMultiplier(0) - 2.0) < 1e-9);
}

// ---- Scorer::ageDecay: tier shapes the curve -----------------------------

TEST("ageDecay: critical never decays even after 1000 days") {
    auto& s = Scorer::instance();
    int64_t t = now() - 1000 * 86400;
    double d = s.ageDecay(t, /*importance=*/3);
    ASSERT_TRUE(d > 0.999);  // effectively 1.0 — frozen
}

TEST("ageDecay: medium at ~90 days ≈ 0.5 (half-life baseline)") {
    auto& s = Scorer::instance();
    int64_t t = now() - 90 * 86400;
    double d = s.ageDecay(t, /*importance=*/1);
    // Half-life ~90d → expect ~0.5. Tolerance 0.1 for clock drift.
    ASSERT_TRUE(d > 0.4 && d < 0.6);
}

TEST("ageDecay: high decays slower than medium at same age") {
    auto& s = Scorer::instance();
    int64_t t = now() - 90 * 86400;
    double d_med  = s.ageDecay(t, /*importance=*/1);
    double d_high = s.ageDecay(t, /*importance=*/2);
    ASSERT_TRUE(d_high > d_med);  // slower decay → higher residual
}

TEST("ageDecay: low decays faster than medium at same age") {
    auto& s = Scorer::instance();
    int64_t t = now() - 90 * 86400;
    double d_med = s.ageDecay(t, /*importance=*/1);
    double d_low = s.ageDecay(t, /*importance=*/0);
    ASSERT_TRUE(d_low < d_med);  // faster decay → lower residual
}

TEST("ageDecay: created_at=0 (unknown) returns 1.0 regardless of tier") {
    auto& s = Scorer::instance();
    ASSERT_TRUE(std::fabs(s.ageDecay(0, 0) - 1.0) < 1e-9);
    ASSERT_TRUE(std::fabs(s.ageDecay(0, 1) - 1.0) < 1e-9);
    ASSERT_TRUE(std::fabs(s.ageDecay(0, 2) - 1.0) < 1e-9);
    ASSERT_TRUE(std::fabs(s.ageDecay(0, 3) - 1.0) < 1e-9);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
