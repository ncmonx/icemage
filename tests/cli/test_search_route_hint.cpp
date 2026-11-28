// test_search_route_hint.cpp -- wrong-tool / miss hints (2026-11-28).
#include "../test_main.hpp"
#include "../../src/cli/search_route_hint.hpp"

using namespace icmg::cli;

TEST("route_hint: code names vs prose") {
    ASSERT_TRUE(looksLikeCodeName("hasShellOperators"));
    ASSERT_TRUE(looksLikeCodeName("run_args"));
    ASSERT_TRUE(looksLikeCodeName("core::Migrator"));
    ASSERT_TRUE(looksLikeCodeName("find_slices.hpp"));
    ASSERT_TRUE(looksLikeCodeName("src/tkil/runner.cpp"));
    ASSERT_TRUE(!looksLikeCodeName("release flow"));
    ASSERT_TRUE(!looksLikeCodeName("why did the build fail on windows"));
    ASSERT_TRUE(!looksLikeCodeName("_private"));          // leading _ alone is not enough
    ASSERT_TRUE(!looksLikeCodeName(""));
}

TEST("route_hint: recall hint points to symbol / file lookups") {
    ASSERT_TRUE(recallCodeHint("release flow").empty());
    auto h = recallCodeHint("parseArgv");
    ASSERT_TRUE(h.find("icmg graph symbol parseArgv") != std::string::npos);
    ASSERT_TRUE(h.find("icmg grep parseArgv --smart") != std::string::npos);
    auto f = recallCodeHint("runner.cpp");
    ASSERT_TRUE(f.find("icmg find --name runner.cpp") != std::string::npos);
}

TEST("route_hint: find miss suggests the longest token + fewer words") {
    auto h = findMissHint("where is the xyzzyHandler wired up now");
    ASSERT_TRUE(h.find("--name xyzzyHandler") != std::string::npos);
    ASSERT_TRUE(h.find("fewer") != std::string::npos);
    ASSERT_TRUE(findMissHint("ab").empty());
    ASSERT_TRUE(findMissHint("quux").find("fewer") == std::string::npos);
    ASSERT_TRUE(nameMissHint("GraphStore").find("icmg find \"GraphStore\"") != std::string::npos);
    ASSERT_TRUE(nameMissHint("").empty());
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
