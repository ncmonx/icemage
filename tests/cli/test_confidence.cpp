// v1.61 F10: confidence-band scoring helpers.

#include "../test_main.hpp"
#include "../../src/cli/router.hpp"

using namespace icmg::cli;

TEST("confidence: band boundaries") {
    ASSERT_TRUE(confidenceBand(0.0)  == ConfidenceBand::Low);
    ASSERT_TRUE(confidenceBand(0.39) == ConfidenceBand::Low);
    ASSERT_TRUE(confidenceBand(0.40) == ConfidenceBand::Medium);
    ASSERT_TRUE(confidenceBand(0.74) == ConfidenceBand::Medium);
    ASSERT_TRUE(confidenceBand(0.75) == ConfidenceBand::High);
    ASSERT_TRUE(confidenceBand(1.0)  == ConfidenceBand::High);
}

TEST("confidence: isLowConfidence threshold") {
    ASSERT_TRUE(isLowConfidence(0.39));
    ASSERT_FALSE(isLowConfidence(0.40));
    ASSERT_FALSE(isLowConfidence(0.9));
}

TEST("confidence: band names") {
    ASSERT_EQ(std::string(confidenceBandName(ConfidenceBand::Low)),    std::string("low"));
    ASSERT_EQ(std::string(confidenceBandName(ConfidenceBand::Medium)), std::string("medium"));
    ASSERT_EQ(std::string(confidenceBandName(ConfidenceBand::High)),   std::string("high"));
}

TEST("confidence: classifyPrompt decision carries a usable confidence") {
    auto d = classifyPrompt("refactor the authentication module and run the tests");
    ASSERT_TRUE(d.confidence >= 0.0 && d.confidence <= 1.0);
    // band of that confidence is one of the three valid enum values
    auto b = confidenceBand(d.confidence);
    ASSERT_TRUE(b == ConfidenceBand::Low || b == ConfidenceBand::Medium || b == ConfidenceBand::High);
}
