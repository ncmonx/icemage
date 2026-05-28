// v1.58 F3: bloom filter unit tests.

#include "../test_main.hpp"
#include "../../src/graph/node_bloom.hpp"

#include <string>

using namespace icmg::graph;

TEST("bloom: before build, maybeContains always true (conservative)") {
    NodeBloom b;
    b.reset(100);
    b.add("src/foo.cpp");
    // Not marked built → cannot trust negatives.
    ASSERT_TRUE(b.maybeContains("anything-at-all"));
}

TEST("bloom: all added keys report maybe-present after build") {
    NodeBloom b;
    b.reset(1000);
    for (int i = 0; i < 1000; ++i)
        b.add("src/file_" + std::to_string(i) + ".cpp");
    b.markBuilt();
    for (int i = 0; i < 1000; ++i)
        ASSERT_TRUE(b.maybeContains("src/file_" + std::to_string(i) + ".cpp"));
}

TEST("bloom: false-positive rate under 1% on absent keys") {
    NodeBloom b;
    b.reset(1000);
    for (int i = 0; i < 1000; ++i)
        b.add("present/" + std::to_string(i));
    b.markBuilt();

    int fp = 0, trials = 10000;
    for (int i = 0; i < trials; ++i) {
        // Keys guaranteed not added (different namespace).
        if (b.maybeContains("absent/" + std::to_string(i))) ++fp;
    }
    double rate = (double)fp / trials;
    ASSERT_TRUE(rate < 0.01);   // < 1% FP at full load with k=7, m=10n
}

TEST("bloom: definitely-absent short-circuits (at least some false)") {
    NodeBloom b;
    b.reset(100);
    b.add("only/one/key");
    b.markBuilt();
    // A random key very likely hashes to at least one clear bit.
    bool any_absent = false;
    for (int i = 0; i < 100; ++i) {
        if (!b.maybeContains("nope/" + std::to_string(i))) { any_absent = true; break; }
    }
    ASSERT_TRUE(any_absent);
}

TEST("bloom: reset clears state") {
    NodeBloom b;
    b.reset(100);
    b.add("x/y/z");
    b.markBuilt();
    ASSERT_TRUE(b.addedCount() == 1u);
    b.reset(100);
    ASSERT_TRUE(b.addedCount() == 0u);
    ASSERT_FALSE(b.built());
}

TEST("bloom: empty filter add auto-sizes") {
    NodeBloom b;            // no reset called
    b.add("auto/size");
    b.markBuilt();
    ASSERT_TRUE(b.maybeContains("auto/size"));
    ASSERT_TRUE(b.bitCount() > 0u);
}
