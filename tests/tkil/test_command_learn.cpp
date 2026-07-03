// tests/tkil/test_command_learn.cpp
// D6 cross-session learning -- unit tests for the pure command-stats analyzer.
#include "../test_main.hpp"
#include "../../src/tkil/command_learn.hpp"

using namespace icmg::tkil;

// A frequently-run command with big output that mostly gets filtered -> Noisy.
TEST("learn: frequent large noisy command is flagged") {
    CmdStat s{"cargo build", /*freq*/10, /*orig*/2000, /*filt*/200};
    LearnResult r = analyzeOne(s);
    ASSERT_TRUE(r.cls == LearnClass::Noisy);
    ASSERT_TRUE(r.avg_original >= 40.0);
    ASSERT_TRUE(r.filter_ratio <= 0.6);
    // build-like -> recommend --nano
    ASSERT_CONTAINS(r.recommendation, "--nano");
}

// Non-build noisy command -> recommend --gist.
TEST("learn: noisy non-build command recommends --gist") {
    CmdStat s{"kubectl get pods -A", /*freq*/8, /*orig*/1600, /*filt*/300};
    LearnResult r = analyzeOne(s);
    ASSERT_TRUE(r.cls == LearnClass::Noisy);
    ASSERT_CONTAINS(r.recommendation, "--gist");
}

// Tiny output -> Quiet, never flagged regardless of frequency.
TEST("learn: tiny output is quiet, not flagged") {
    CmdStat s{"git status --porcelain", /*freq*/50, /*orig*/100, /*filt*/90};
    LearnResult r = analyzeOne(s);
    ASSERT_TRUE(r.cls == LearnClass::Quiet);
    ASSERT_TRUE(r.recommendation.empty());
}

// Rarely-run command (below min_frequency) -> not enough evidence -> Normal.
TEST("learn: infrequent command is not flagged (insufficient evidence)") {
    CmdStat s{"cargo build", /*freq*/1, /*orig*/500, /*filt*/50};
    LearnResult r = analyzeOne(s);
    ASSERT_TRUE(r.cls == LearnClass::Normal);
    ASSERT_TRUE(r.recommendation.empty());
}

// Large output that survives the filter (high ratio) -> not noise -> Normal.
TEST("learn: large but high-value output is not noisy") {
    CmdStat s{"cat report.txt", /*freq*/5, /*orig*/500, /*filt*/480};
    LearnResult r = analyzeOne(s);
    ASSERT_TRUE(r.cls == LearnClass::Normal);
}

// analyzeCommands returns only noisy, sorted by waste (avg * (1-ratio)) desc.
TEST("learn: corpus returns noisy sorted by waste desc") {
    std::vector<CmdStat> corpus = {
        {"git status", 50, 100, 90},          // quiet
        {"cargo build", 10, 2000, 200},       // noisy, waste=200*0.9=180
        {"npm test", 6, 900, 450},            // noisy, waste=150*0.5=75
        {"echo hi", 3, 3, 3},                 // quiet
    };
    auto out = analyzeCommands(corpus);
    ASSERT_EQ((int)out.size(), 2);
    ASSERT_EQ(out[0].command, std::string("cargo build"));  // highest waste first
    ASSERT_EQ(out[1].command, std::string("npm test"));
}

// Build-detection helper.
TEST("learn: build-like command detection") {
    ASSERT_TRUE(learnLooksLikeBuild("cmake --build build"));
    ASSERT_TRUE(learnLooksLikeBuild("ctest --output-on-failure"));
    ASSERT_TRUE(learnLooksLikeBuild("CARGO BUILD --release"));
    ASSERT_FALSE(learnLooksLikeBuild("kubectl get pods"));
    ASSERT_FALSE(learnLooksLikeBuild("git log"));
}
