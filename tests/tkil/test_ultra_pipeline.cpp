// v1.56 T1: ultra pipeline orchestrator tests.

#include "../test_main.hpp"
#include "../../src/tkil/ultra_pipeline.hpp"

#include <string>

using namespace icmg::tkil;

TEST("duplicationRatio: empty returns 0") {
    ASSERT_EQ(duplicationRatio(""), 0.0);
}

TEST("duplicationRatio: all distinct returns 0") {
    ASSERT_EQ(duplicationRatio("a\nb\nc\nd\n"), 0.0);
}

TEST("duplicationRatio: half duplicate returns 0.5") {
    // 4 lines, 2 distinct -> ratio (4-2)/4 = 0.5
    double r = duplicationRatio("x\nx\ny\ny\n");
    ASSERT_TRUE(r > 0.49 && r < 0.51);
}

TEST("autoTriggerUltra: tiny output does not trigger") {
    ASSERT_FALSE(autoTriggerUltra("short text"));
}

TEST("autoTriggerUltra: long but distinct does not trigger") {
    std::string in;
    for (int i = 0; i < 1000; ++i) in += "line-" + std::to_string(i) + "\n";
    // 1000 distinct lines, ratio 0 -> no trigger even at 9 KB
    ASSERT_FALSE(autoTriggerUltra(in));
}

TEST("autoTriggerUltra: long and duplicate triggers") {
    std::string in;
    // 6 KB of repeated lines, dup-ratio ~0.5
    for (int i = 0; i < 600; ++i) in += "same-line-repeated-many-times\n";
    ASSERT_TRUE(autoTriggerUltra(in));
}

TEST("applyUltraPipeline: empty input returns empty") {
    ASSERT_EQ(applyUltraPipeline("", "anything"), std::string(""));
}

TEST("applyUltraPipeline: non-noisy input passes through largely intact") {
    // Use lines with low shared-prefix so dedup-pass's near-dup (>=80%
    // shared prefix) does NOT collapse them.
    std::string in = "first distinct line\nanother different one\ncompletely unrelated\n";
    std::string out = applyUltraPipeline(in, "echo hello");
    // No dedup (no near-dup), no profile, no outcome.
    // Glossary may tokenise nothing (first encounter of each line).
    ASSERT_TRUE(out.find("first distinct line") != std::string::npos);
    ASSERT_TRUE(out.find("another different one") != std::string::npos);
    ASSERT_TRUE(out.find("completely unrelated") != std::string::npos);
}

TEST("applyUltraPipeline: noisy cmake build collapses heavily") {
    std::string in;
    for (int i = 1; i <= 100; ++i) {
        in += "[" + std::to_string(i) + "/100] Building CXX object src/foo_"
              + std::to_string(i) + ".cpp.o\n";
    }
    in += "[100/100] Linking CXX executable myapp\n";
    std::string out = applyUltraPipeline(in, "cmake --build build --target myapp");
    // Outcome-only kicks in for cmake-build, keeping ONLY the final Linking line.
    ASSERT_TRUE(out.find("Linking CXX executable myapp") != std::string::npos);
    ASSERT_TRUE(out.size() < in.size() / 10);
}

TEST("applyUltraPipeline: error lines survive every stage") {
    std::string in =
        "[1/3] Building CXX object foo.cpp.o\n"
        "src/foo.cpp(42): error C2065: undeclared identifier 'bar'\n"
        "FAILED: [code=1]\n";
    std::string out = applyUltraPipeline(in, "cmake --build build");
    ASSERT_TRUE(out.find("C2065") != std::string::npos);
    ASSERT_TRUE(out.find("FAILED") != std::string::npos);
}
