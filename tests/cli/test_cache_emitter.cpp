// Phase 40 T1: cache emitter unit tests.
#include "../test_main.hpp"
#include "../../src/cli/cache_emitter.hpp"

using icmg::cli::wrapCachePrefix;
using icmg::cli::hasCacheWrap;
using icmg::cli::CacheEmitOptions;

TEST("cache: wrapCachePrefix wraps non-empty input") {
    auto out = wrapCachePrefix("stable content");
    ASSERT_TRUE(hasCacheWrap(out));
    ASSERT_TRUE(out.find("ttl=3600") != std::string::npos);
    ASSERT_TRUE(out.find("stable content") != std::string::npos);
}

TEST("cache: wrap idempotent — already-wrapped pass-through") {
    auto first = wrapCachePrefix("X");
    auto twice = wrapCachePrefix(first);
    ASSERT_EQ(first, twice);
}

TEST("cache: empty input → empty output") {
    auto out = wrapCachePrefix("");
    ASSERT_EQ(out, std::string(""));
}

TEST("cache: custom ttl emitted") {
    CacheEmitOptions o; o.ttl_seconds = 300;
    auto out = wrapCachePrefix("data", o);
    ASSERT_TRUE(out.find("ttl=300") != std::string::npos);
}

TEST("cache: hasCacheWrap detects partial markers correctly") {
    ASSERT_FALSE(hasCacheWrap("plain text"));
    ASSERT_FALSE(hasCacheWrap("<<CACHED>> only opener"));
    ASSERT_TRUE(hasCacheWrap("<<CACHED>>X<</CACHED>>"));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
