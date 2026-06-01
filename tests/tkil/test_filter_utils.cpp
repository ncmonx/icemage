#include "../test_main.hpp"
#include "../../src/tkil/filters/filter_utils.hpp"

using namespace icmg::tkil;

// ---- filter_utils unit tests -----------------------------------------------

TEST("splitLines: basic") {
    auto lines = splitLines("a\nb\nc");
    ASSERT_EQ(lines.size(), 3u);
    ASSERT_EQ(lines[0], std::string("a"));
    ASSERT_EQ(lines[2], std::string("c"));
}

TEST("splitLines: CRLF stripped") {
    auto lines = splitLines("a\r\nb\r\n");
    ASSERT_EQ(lines[0], std::string("a"));
    ASSERT_EQ(lines[1], std::string("b"));
}

TEST("splitLines: empty string") {
    auto lines = splitLines("");
    ASSERT_TRUE(lines.empty());
}

TEST("splitLines: single line no newline") {
    auto lines = splitLines("hello");
    ASSERT_EQ(lines.size(), 1u);
    ASSERT_EQ(lines[0], std::string("hello"));
}

TEST("containsAny: matches keyword") {
    ASSERT_TRUE(containsAny("error: something went wrong", {"error", "warning"}));
}

TEST("containsAny: case-insensitive") {
    ASSERT_TRUE(containsAny("ERROR: bad", {"error"}));
}

TEST("containsAny: no match") {
    ASSERT_FALSE(containsAny("info: all good", {"error", "warning"}));
}

TEST("containsAny: empty keywords") {
    ASSERT_FALSE(containsAny("anything", {}));
}

TEST("applyHardLimit: under limit — no truncation") {
    FilterResult fr;
    fr.output         = "line1\nline2\n";
    fr.original_lines = 2;
    fr.filtered_lines = 2;
    fr.was_truncated  = false;

    auto result = applyHardLimit(fr);
    ASSERT_FALSE(result.was_truncated);
    ASSERT_EQ(result.filtered_lines, 2);
}

TEST("applyHardLimit: over limit — truncates") {
    std::string big;
    for (int i = 0; i < MAX_OUTPUT_LINES + 50; ++i)
        big += "line " + std::to_string(i) + "\n";

    FilterResult fr;
    fr.output         = big;
    fr.original_lines = MAX_OUTPUT_LINES + 50;
    fr.filtered_lines = MAX_OUTPUT_LINES + 50;
    fr.was_truncated  = false;

    auto result = applyHardLimit(fr);
    ASSERT_TRUE(result.was_truncated);
    ASSERT_CONTAINS(result.output, "truncated at");
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
