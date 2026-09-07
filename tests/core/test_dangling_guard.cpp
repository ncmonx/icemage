// TDD (2026-09-07): token-killer C -- dangling-reference guard.
#include "../test_main.hpp"
#include "../../src/core/dangling_guard.hpp"

using namespace icmg::core;

TEST("dangling: dropped definition of referenced entity pulled back") {
    std::vector<std::string> lines = {
        "RuleDaemon manages the pipe lifecycle",   // 0: definition (dropped)
        "some filler line about nothing",          // 1: dropped
        "then RuleDaemon exits after release",     // 2: kept, references entity
    };
    std::vector<bool> keep = {false, false, true};
    auto pull = danglingRepairLines(lines, keep);
    ASSERT_EQ((int)pull.size(), 1);
    ASSERT_EQ((int)pull[0], 0);
}

TEST("dangling: kept definition needs no repair") {
    std::vector<std::string> lines = {
        "RuleDaemon manages the pipe lifecycle",
        "then RuleDaemon exits after release",
    };
    std::vector<bool> keep = {true, true};
    ASSERT_EQ((int)danglingRepairLines(lines, keep).size(), 0);
}

TEST("dangling: plain lowercase words are not entities") {
    std::vector<std::string> lines = {
        "the server was slow yesterday",
        "another line",
        "the server is fine today",
    };
    std::vector<bool> keep = {false, false, true};
    ASSERT_EQ((int)danglingRepairLines(lines, keep).size(), 0);
}

TEST("dangling: snake_case and ALL_CAPS count as entities") {
    std::vector<std::string> lines = {
        "set ICMG_NO_DAEMON to disable spawning",   // 0: def (dropped)
        "rule_daemon_client reads the env",         // 1: def (dropped)
        "when ICMG_NO_DAEMON is 1 the rule_daemon_client skips",  // 2: kept
    };
    std::vector<bool> keep = {false, false, true};
    auto pull = danglingRepairLines(lines, keep);
    ASSERT_EQ((int)pull.size(), 2);
}

TEST("dangling: pullback capped") {
    std::vector<std::string> lines;
    std::vector<bool> keep;
    for (int i = 0; i < 40; ++i) {
        lines.push_back("Entity" + std::to_string(i) + " is defined here");
        keep.push_back(false);
    }
    std::string user_line = "uses";
    for (int i = 0; i < 40; ++i) user_line += " Entity" + std::to_string(i);
    lines.push_back(user_line);
    keep.push_back(true);
    auto pull = danglingRepairLines(lines, keep, 5);
    ASSERT_EQ((int)pull.size(), 5);
}

TEST("dangling: empty and mismatched input safe") {
    ASSERT_EQ((int)danglingRepairLines({}, {}).size(), 0);
    ASSERT_EQ((int)danglingRepairLines({"x"}, {true, false}).size(), 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
