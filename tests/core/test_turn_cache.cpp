// v1.17.0: turn_cache unit tests.

#include "../test_main.hpp"
#include "../../src/core/turn_cache.hpp"

#include <thread>
#include <chrono>

namespace tc = icmg::core::turn_cache;

TEST("turn_cache: lookup miss on first call") {
    tc::resetSession();
    std::string ref = tc::lookup("context", "foo.cpp", 1234);
    ASSERT_EQ(ref, std::string(""));
    ASSERT_EQ(tc::misses(), (size_t)1);
    ASSERT_EQ(tc::hits(),   (size_t)0);
}

TEST("turn_cache: lookup hit after recordResult") {
    tc::resetSession();
    (void)tc::lookup("context", "bar.cpp", 5678);
    tc::recordResult("context", "bar.cpp", 5678, "some content");
    std::string ref = tc::lookup("context", "bar.cpp", 5678);
    ASSERT_TRUE(!ref.empty());
    ASSERT_CONTAINS(ref, "<icmg-cached:");
    ASSERT_EQ(tc::hits(), (size_t)1);
}

TEST("turn_cache: lookup miss on mtime change") {
    tc::resetSession();
    (void)tc::lookup("context", "baz.cpp", 100);
    tc::recordResult("context", "baz.cpp", 100, "v1");
    std::string ref = tc::lookup("context", "baz.cpp", 200);  // different mtime
    ASSERT_EQ(ref, std::string(""));
}

TEST("turn_cache: different args → different keys") {
    tc::resetSession();
    tc::recordResult("context", "a.cpp", 0, "A");
    tc::recordResult("context", "b.cpp", 0, "B");
    std::string ra = tc::lookup("context", "a.cpp", 0);
    std::string rb = tc::lookup("context", "b.cpp", 0);
    ASSERT_TRUE(!ra.empty());
    ASSERT_TRUE(!rb.empty());
    ASSERT_TRUE(ra != rb);  // distinct refs
}

TEST("turn_cache: resetSession clears all") {
    tc::resetSession();
    tc::recordResult("a", "x", 0, "1");
    tc::recordResult("b", "y", 0, "2");
    tc::resetSession();
    std::string r = tc::lookup("a", "x", 0);
    ASSERT_EQ(r, std::string(""));
    ASSERT_EQ(tc::hits(), (size_t)0);
}

TEST("turn_cache: empty content not recorded") {
    tc::resetSession();
    tc::recordResult("ctx", "empty.cpp", 0, "");
    std::string r = tc::lookup("ctx", "empty.cpp", 0);
    ASSERT_EQ(r, std::string(""));  // not stored
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
