// 2026-06-07: pure helpers for `icmg msg check` (#luna-batch).
#include "../test_main.hpp"
#include "../../src/core/wire_check_helpers.hpp"

using namespace icmg::core;

TEST("wire_check: seenMarkerName sanitizes identity to safe filename") {
    ASSERT_EQ(seenMarkerName("pid-1234"), std::string(".seen-pid_1234"));
    ASSERT_EQ(seenMarkerName("a@b.com"), std::string(".seen-a_b_com"));
    ASSERT_EQ(seenMarkerName("clean"), std::string(".seen-clean"));
}

TEST("wire_check: parseSeenTs handles number + garbage") {
    ASSERT_EQ(parseSeenTs("1780799399"), 1780799399LL);
    ASSERT_EQ(parseSeenTs(""), 0LL);
    ASSERT_EQ(parseSeenTs("nope"), 0LL);
}
