// v1.58 F5: command-bigram predictor tests.
//
// Records (prev-cmd -> next-cmd) transitions and predicts the most-likely
// next command. Backs predictive prefetch.

#include "../test_main.hpp"
#include "../../src/core/cmd_bigram.hpp"

#include <string>

using namespace icmg::core;

TEST("bigram: empty predictor returns empty prediction") {
    CmdBigram bg;
    ASSERT_EQ(bg.predictNext("context"), std::string(""));
}

TEST("bigram: single transition predicts the successor") {
    CmdBigram bg;
    bg.record("context", "symbol");
    ASSERT_EQ(bg.predictNext("context"), std::string("symbol"));
}

TEST("bigram: most-frequent successor wins") {
    CmdBigram bg;
    bg.record("context", "symbol");
    bg.record("context", "symbol");
    bg.record("context", "symbol");
    bg.record("context", "recall");
    ASSERT_EQ(bg.predictNext("context"), std::string("symbol"));
}

TEST("bigram: distinct predecessors keep separate distributions") {
    CmdBigram bg;
    bg.record("context", "symbol");
    bg.record("recall", "pack");
    ASSERT_EQ(bg.predictNext("context"), std::string("symbol"));
    ASSERT_EQ(bg.predictNext("recall"), std::string("pack"));
}

TEST("bigram: unknown predecessor returns empty") {
    CmdBigram bg;
    bg.record("a", "b");
    ASSERT_EQ(bg.predictNext("zzz"), std::string(""));
}

TEST("bigram: confidence reflects successor share") {
    CmdBigram bg;
    bg.record("x", "y");
    bg.record("x", "y");
    bg.record("x", "z");
    // y = 2/3 ≈ 0.66
    double c = bg.confidence("x", "y");
    ASSERT_TRUE(c > 0.6 && c < 0.7);
}

TEST("bigram: serialize round-trips") {
    CmdBigram bg;
    bg.record("ctx", "sym");
    bg.record("ctx", "sym");
    std::string blob = bg.serialize();
    CmdBigram restored;
    restored.deserialize(blob);
    ASSERT_EQ(restored.predictNext("ctx"), std::string("sym"));
}
