// `icmg context --head/--tail` pure range-resolver tests (2026-06-28).
// Spec for the helper that closes the head/tail RAW=1 escape.
#include "../test_main.hpp"
#include "../../src/cli/headtail_range.hpp"

using namespace icmg::cli;

TEST("headtail: head 5 of 100 -> 1..5") {
    auto r = resolveHeadTail(100, 5, -1);
    ASSERT_EQ(r.start, 1);
    ASSERT_EQ(r.end, 5);
}

TEST("headtail: tail 5 of 100 -> 96..100") {
    auto r = resolveHeadTail(100, -1, 5);
    ASSERT_EQ(r.start, 96);
    ASSERT_EQ(r.end, 100);
}

TEST("headtail: head larger than file clamps to total") {
    auto r = resolveHeadTail(3, 10, -1);
    ASSERT_EQ(r.start, 1);
    ASSERT_EQ(r.end, 3);
}

TEST("headtail: tail larger than file clamps to 1") {
    auto r = resolveHeadTail(3, -1, 10);
    ASSERT_EQ(r.start, 1);
    ASSERT_EQ(r.end, 3);
}

TEST("headtail: head precedence when both set") {
    auto r = resolveHeadTail(100, 5, 5);
    ASSERT_EQ(r.start, 1);
    ASSERT_EQ(r.end, 5);
}

TEST("headtail: neither requested -> 0,0") {
    auto r = resolveHeadTail(100, -1, -1);
    ASSERT_EQ(r.start, 0);
    ASSERT_EQ(r.end, 0);
}

TEST("headtail: empty file -> 0,0") {
    auto r = resolveHeadTail(0, 5, -1);
    ASSERT_EQ(r.start, 0);
    ASSERT_EQ(r.end, 0);
}

TEST("headtail: tail 1 of 1 -> 1..1") {
    auto r = resolveHeadTail(1, -1, 1);
    ASSERT_EQ(r.start, 1);
    ASSERT_EQ(r.end, 1);
}
