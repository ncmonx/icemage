#include "../test_main.hpp"
#include "../../src/core/session_state.hpp"
#include <filesystem>

using icmg::core::SessionState;

TEST("session_state: fresh instance has empty seen-set") {
    SessionState::instance().clear();
    ASSERT_EQ(SessionState::instance().size(), 0u);
}

TEST("session_state: markSeen + hasSeen roundtrip") {
    SessionState::instance().clear();
    ASSERT_FALSE(SessionState::instance().hasSeen("recall:42"));
    SessionState::instance().markSeen("recall:42");
    ASSERT_TRUE(SessionState::instance().hasSeen("recall:42"));
}

TEST("session_state: distinct keys independent") {
    SessionState::instance().clear();
    SessionState::instance().markSeen("FILE:foo.cpp");
    ASSERT_FALSE(SessionState::instance().hasSeen("FILE:bar.cpp"));
    ASSERT_TRUE(SessionState::instance().hasSeen("FILE:foo.cpp"));
}

TEST("session_state: clear resets all keys") {
    SessionState::instance().markSeen("x");
    SessionState::instance().markSeen("y");
    SessionState::instance().clear();
    ASSERT_FALSE(SessionState::instance().hasSeen("x"));
    ASSERT_FALSE(SessionState::instance().hasSeen("y"));
    ASSERT_EQ(SessionState::instance().size(), 0u);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
