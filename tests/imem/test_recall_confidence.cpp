// TDD (2026-09-23): recall confidence tiers -- IDE-B from the landscape scan
// (MemCalib, arXiv 2609.24259: frontier models over- and under-use injected
// memory; a calibration cue on each hit helps the model weigh it correctly).
// Pure mapping score -> tier label; no DB/IO.
#include "../test_main.hpp"
#include "../../src/imem/recall_confidence.hpp"
#include <string>

using namespace icmg::imem;

// 1. Tier boundaries: strong >= 3x min, solid >= 1.5x, weak below.
TEST("confidence: tiers scale from the min-score floor") {
    const double min_score = 4.0;
    ASSERT_EQ(std::string(confidenceTier(12.0, min_score)), std::string("high"));
    ASSERT_EQ(std::string(confidenceTier(6.0, min_score)), std::string("med"));
    ASSERT_EQ(std::string(confidenceTier(4.0, min_score)), std::string("low"));
}

// 2. Exactly at a boundary belongs to the stronger tier.
TEST("confidence: boundary values are inclusive upward") {
    const double min_score = 2.0;
    ASSERT_EQ(std::string(confidenceTier(6.0, min_score)), std::string("high"));
    ASSERT_EQ(std::string(confidenceTier(3.0, min_score)), std::string("med"));
}

// 3. Degenerate floor (<= 0) never divides by zero; anything positive = high.
TEST("confidence: zero floor safe") {
    ASSERT_EQ(std::string(confidenceTier(5.0, 0.0)), std::string("high"));
    ASSERT_EQ(std::string(confidenceTier(0.0, 0.0)), std::string("low"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
