// #290: `icmg context` cached reads keyed on a hand-picked flag list, so a
// flag outside it (--head/--tail, --skeleton, ...) replayed a cached read made
// WITHOUT it -- `icmg context f --head 3` printed the whole file. The key now
// covers the full argv plus the file identity.

#include "../test_main.hpp"
#include "../../src/cli/context_cache_key.hpp"

using icmg::cli::contextCacheKey;

TEST("contextCacheKey: --head/--tail/--skeleton change the key") {
    const std::vector<std::string> plain = {"f.cpp"};
    const auto base = contextCacheKey("/p/f.cpp", 100, 42, plain);
    ASSERT_TRUE(base != contextCacheKey("/p/f.cpp", 100, 42, {"f.cpp", "--head", "3"}));
    ASSERT_TRUE(base != contextCacheKey("/p/f.cpp", 100, 42, {"f.cpp", "--tail", "3"}));
    ASSERT_TRUE(base != contextCacheKey("/p/f.cpp", 100, 42, {"f.cpp", "--skeleton"}));
    ASSERT_TRUE(contextCacheKey("/p/f.cpp", 100, 42, {"f.cpp", "--head", "3"})
             != contextCacheKey("/p/f.cpp", 100, 42, {"f.cpp", "--head", "30"}));
}

TEST("contextCacheKey: same argv + same file identity -> same key") {
    const std::vector<std::string> a = {"f.cpp", "--lines", "1-9"};
    ASSERT_EQ(contextCacheKey("/p/f.cpp", 100, 42, a), contextCacheKey("/p/f.cpp", 100, 42, a));
}

TEST("contextCacheKey: file change (mtime/size/path) changes the key") {
    const std::vector<std::string> a = {"f.cpp"};
    const auto k = contextCacheKey("/p/f.cpp", 100, 42, a);
    ASSERT_TRUE(k != contextCacheKey("/p/f.cpp", 101, 42, a));
    ASSERT_TRUE(k != contextCacheKey("/p/f.cpp", 100, 43, a));
    ASSERT_TRUE(k != contextCacheKey("/q/f.cpp", 100, 42, a));
}

TEST("contextCacheKey: argument boundaries are not ambiguous") {
    ASSERT_TRUE(contextCacheKey("/p/f", 1, 1, {"--head", "3"})
             != contextCacheKey("/p/f", 1, 1, {"--head3"}));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
