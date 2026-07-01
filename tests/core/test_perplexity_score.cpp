// A3 (2026-07-01): pure surprisal->salience adapter for perplexity-based
// compression (LLMLingua-style self-information). No llama here -- feed it
// per-token surprisals (-log p) and span boundaries; it produces per-span
// salience scores for selectByBudget. Fully unit-testable with synthetic data.
#include "../test_main.hpp"
#include "../../src/core/perplexity_score.hpp"

using namespace icmg::core;

// --- spanSalience: mean surprisal per span ---

TEST("perplexity: spanSalience averages token surprisals per span") {
    // 5 tokens, 2 spans: [3 tokens][2 tokens]
    std::vector<float> surp = {1.0f, 2.0f, 3.0f, 10.0f, 20.0f};
    std::vector<int> counts = {3, 2};
    auto sal = spanSalience(surp, counts);
    ASSERT_EQ((int)sal.size(), 2);
    ASSERT_TRUE(sal[0] > 1.99 && sal[0] < 2.01);   // mean(1,2,3)=2
    ASSERT_TRUE(sal[1] > 14.99 && sal[1] < 15.01); // mean(10,20)=15
}

TEST("perplexity: spanSalience handles empty span as zero") {
    std::vector<float> surp = {5.0f, 5.0f};
    std::vector<int> counts = {2, 0};  // second span has no tokens
    auto sal = spanSalience(surp, counts);
    ASSERT_EQ((int)sal.size(), 2);
    ASSERT_TRUE(sal[0] > 4.99 && sal[0] < 5.01);
    ASSERT_TRUE(sal[1] == 0.0);
}

TEST("perplexity: spanSalience clamps counts exceeding surprisal length") {
    std::vector<float> surp = {4.0f};
    std::vector<int> counts = {5};  // asks for 5 but only 1 available
    auto sal = spanSalience(surp, counts);
    ASSERT_EQ((int)sal.size(), 1);
    ASSERT_TRUE(sal[0] > 3.99 && sal[0] < 4.01);  // averages what's there
}

// --- normalizeUnit: min-max to [0,1] ---

TEST("perplexity: normalizeUnit maps min->0 max->1") {
    std::vector<double> in = {2.0, 15.0, 8.5};
    auto out = normalizeUnit(in);
    ASSERT_EQ((int)out.size(), 3);
    ASSERT_TRUE(out[0] == 0.0);   // min
    ASSERT_TRUE(out[1] == 1.0);   // max
    ASSERT_TRUE(out[2] > 0.49 && out[2] < 0.51);  // midpoint
}

TEST("perplexity: normalizeUnit constant input -> neutral 0.5") {
    std::vector<double> in = {7.0, 7.0, 7.0};
    auto out = normalizeUnit(in);
    for (double v : out) ASSERT_TRUE(v > 0.49 && v < 0.51);
}

TEST("perplexity: normalizeUnit empty -> empty") {
    std::vector<double> in;
    ASSERT_TRUE(normalizeUnit(in).empty());
}

// --- end-to-end: high-surprisal span ranks above low-surprisal span ---

TEST("perplexity: informative (high-surprisal) span scores higher") {
    // span0 = predictable/boilerplate (low surprisal), span1 = informative
    std::vector<float> surp = {0.5f, 0.5f, 9.0f, 9.0f};
    std::vector<int> counts = {2, 2};
    auto sal = normalizeUnit(spanSalience(surp, counts));
    ASSERT_TRUE(sal[1] > sal[0]);  // keep the informative span
    ASSERT_TRUE(sal[0] == 0.0 && sal[1] == 1.0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
