// v2.1 C3: U-shaped ordering — counters "lost-in-the-middle". Highest-importance
// slices belong at the edges (front+back), lowest in the dead-zone middle.
#include "../test_main.hpp"
#include "../../src/core/u_shaped.hpp"
#include <string>
#include <vector>
using namespace icmg::core;

// Helper: build a 0..n-1 index vector to make the permutation easy to assert.
static std::vector<int> iota(int n) {
    std::vector<int> v;
    for (int i = 0; i < n; ++i) v.push_back(i);
    return v;
}

TEST("reorderUShaped: empty input -> empty output") {
    std::vector<int> in;
    auto out = reorderUShaped(in);
    ASSERT_EQ(out.size(), (size_t)0);
}

TEST("reorderUShaped: single item -> unchanged") {
    std::vector<int> in{42};
    auto out = reorderUShaped(in);
    ASSERT_EQ(out.size(), (size_t)1);
    ASSERT_EQ(out[0], 42);
}

TEST("reorderUShaped: two items -> rank0 front, rank1 back") {
    // input sorted DESC by importance: [hi, lo]
    std::vector<int> in{0, 1};
    auto out = reorderUShaped(in);
    ASSERT_EQ(out.size(), (size_t)2);
    ASSERT_EQ(out[0], 0);  // highest at front edge
    ASSERT_EQ(out[1], 1);  // next at back edge
}

TEST("reorderUShaped: odd count (5) -> [0,2,4,3,1]") {
    auto in = iota(5);  // ranks 0(hi)..4(lo)
    auto out = reorderUShaped(in);
    std::vector<int> want{0, 2, 4, 3, 1};
    ASSERT_EQ(out.size(), (size_t)5);
    for (size_t i = 0; i < want.size(); ++i)
        ASSERT_EQ(out[i], want[i]);
}

TEST("reorderUShaped: even count (6) -> [0,2,4,5,3,1]") {
    auto in = iota(6);  // ranks 0(hi)..5(lo)
    auto out = reorderUShaped(in);
    std::vector<int> want{0, 2, 4, 5, 3, 1};
    ASSERT_EQ(out.size(), (size_t)6);
    for (size_t i = 0; i < want.size(); ++i)
        ASSERT_EQ(out[i], want[i]);
}

TEST("reorderUShaped: strictly-descending input -> two largest at the edges") {
    // importance value == item value here (DESC sorted): 100 is highest.
    std::vector<int> in{100, 90, 80, 70, 60, 50, 40};
    auto out = reorderUShaped(in);
    ASSERT_EQ(out.size(), in.size());
    // front edge holds the single largest; back edge holds the 2nd largest.
    ASSERT_EQ(out.front(), 100);
    ASSERT_EQ(out.back(), 90);
    // the global minimum must NOT sit at an edge (it belongs in the middle).
    ASSERT_TRUE(out.front() != 40 && out.back() != 40);
    // it is a permutation: same multiset, same size.
    int sumIn = 0, sumOut = 0;
    for (int x : in) sumIn += x;
    for (int x : out) sumOut += x;
    ASSERT_EQ(sumIn, sumOut);
}

TEST("reorderUShaped: middle holds the lowest-importance item (odd)") {
    auto in = iota(5);  // 4 is lowest importance
    auto out = reorderUShaped(in);
    ASSERT_EQ(out[2], 4);  // dead-center = least important
}

TEST("reorderUShaped: result is a permutation of the input (string overload)") {
    std::vector<std::string> in{"a", "b", "c", "d"};
    auto out = reorderUShaped(in);
    ASSERT_EQ(out.size(), in.size());
    std::vector<std::string> want{"a", "c", "d", "b"};
    for (size_t i = 0; i < want.size(); ++i)
        ASSERT_EQ(out[i], want[i]);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
