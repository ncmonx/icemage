// v1.79 M5 smart-bisect: pure bisect-core tests.
#include "../test_main.hpp"
#include "../../src/cli/bisect_logic.hpp"

using namespace icmg::cli;

TEST("bisect: firstBadIndex finds boundary (monotonic good->bad)") {
    // commits 0..9, first bad at index 6
    auto isBad = [](int i) { return i >= 6; };
    ASSERT_EQ(firstBadIndex(10, isBad), 6);
}

TEST("bisect: first commit bad") {
    ASSERT_EQ(firstBadIndex(5, [](int){ return true; }), 0);
}

TEST("bisect: none bad returns -1") {
    ASSERT_EQ(firstBadIndex(5, [](int){ return false; }), -1);
}

TEST("bisect: only last bad") {
    auto isBad = [](int i) { return i == 4; };  // monotonic tail
    ASSERT_EQ(firstBadIndex(5, isBad), 4);
}

TEST("bisect: steps estimate log2 of interior") {
    ASSERT_EQ(bisectStepsEstimate(2), 0);    // endpoints only
    ASSERT_EQ(bisectStepsEstimate(10), 4);   // 8 interior -> ceil(log2(9)) = 4
    ASSERT_EQ(bisectStepsEstimate(3), 1);    // 1 interior -> 1 step
}
