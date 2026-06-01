// Phase 68 T2: unit tests for line-set-diff used by `pack --diff`.
#include "../test_main.hpp"
#include "../../src/cli/pack_delta.hpp"

using icmg::cli::computePackDelta;

TEST("delta: identical inputs → only blank lines retained") {
    std::string s = "alpha\nbeta\ngamma\n";
    auto d = computePackDelta(s, s);
    // No new lines except blanks (none in input) → empty.
    ASSERT_TRUE(d.empty() || d.find("alpha") == std::string::npos);
}

TEST("delta: completely new content → full content emitted") {
    std::string prev = "old1\nold2\n";
    std::string cur  = "new1\nnew2\n";
    auto d = computePackDelta(prev, cur);
    ASSERT_TRUE(d.find("new1") != std::string::npos);
    ASSERT_TRUE(d.find("new2") != std::string::npos);
}

TEST("delta: shared lines dropped, new lines kept") {
    std::string prev = "shared\nold\n";
    std::string cur  = "shared\nnew\n";
    auto d = computePackDelta(prev, cur);
    ASSERT_TRUE(d.find("shared") == std::string::npos);
    ASSERT_TRUE(d.find("new") != std::string::npos);
}

TEST("delta: blank lines preserve structure") {
    std::string prev = "x\n";
    std::string cur  = "x\n\nnew\n\n";
    auto d = computePackDelta(prev, cur);
    // x dropped, blanks + new kept
    ASSERT_TRUE(d.find("x") == std::string::npos);
    ASSERT_TRUE(d.find("new") != std::string::npos);
    ASSERT_TRUE(d.find("\n\n") != std::string::npos);
}

TEST("delta: empty prev → entire cur passes") {
    std::string d = computePackDelta("", "fresh\nlines\n");
    ASSERT_TRUE(d.find("fresh") != std::string::npos);
    ASSERT_TRUE(d.find("lines") != std::string::npos);
}

TEST("delta: empty cur → empty result") {
    auto d = computePackDelta("anything\n", "");
    ASSERT_TRUE(d.empty());
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
