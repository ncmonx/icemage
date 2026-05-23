// v1.17.0: inject_dedup unit tests.

#include "../test_main.hpp"
#include "../../src/core/inject_dedup.hpp"

namespace dd = icmg::core::inject_dedup;

TEST("inject_dedup: first call returns false (not seen)") {
    dd::resetSession();
    bool seen = dd::seenBefore("hello world");
    ASSERT_FALSE(seen);
    ASSERT_EQ(dd::uniqueCount(), (size_t)1);
}

TEST("inject_dedup: second call same content returns true") {
    dd::resetSession();
    (void)dd::seenBefore("foo");
    bool seen = dd::seenBefore("foo");
    ASSERT_TRUE(seen);
    ASSERT_EQ(dd::uniqueCount(), (size_t)1);  // still 1 unique
}

TEST("inject_dedup: different content treated independently") {
    dd::resetSession();
    (void)dd::seenBefore("A");
    bool seenB = dd::seenBefore("B");
    ASSERT_FALSE(seenB);
    bool seenA2 = dd::seenBefore("A");
    ASSERT_TRUE(seenA2);
    ASSERT_EQ(dd::uniqueCount(), (size_t)2);
}

TEST("inject_dedup: empty content never deduped") {
    dd::resetSession();
    (void)dd::seenBefore("");
    bool seen2 = dd::seenBefore("");
    ASSERT_FALSE(seen2);  // empty always returns false
    ASSERT_EQ(dd::uniqueCount(), (size_t)0);
}

TEST("inject_dedup: resetSession clears state") {
    dd::resetSession();
    (void)dd::seenBefore("x");
    (void)dd::seenBefore("y");
    dd::resetSession();
    bool seen = dd::seenBefore("x");
    ASSERT_FALSE(seen);
    ASSERT_EQ(dd::uniqueCount(), (size_t)1);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
