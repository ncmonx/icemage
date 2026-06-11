// Post-change ritual gate — pure-core unit tests (TDD-first, 2026-06-11).
// Positive-side enforcement: after a code change the 5-sync ritual is owed
// until the REQUIRED steps (graph/store/wflog) are observed.

#include "../test_main.hpp"
#include "../../src/cli/ritual_gate.hpp"

using namespace icmg::cli;

TEST("ritual: no change -> never owed") {
    RitualState s;                       // changed = false
    auto v = evaluateRitual(s);
    ASSERT_FALSE(v.owed);
    ASSERT_TRUE(v.missing.empty());
}

TEST("ritual: change with nothing done -> owed, both required missing") {
    RitualState s; s.changed = true;
    auto v = evaluateRitual(s);
    ASSERT_TRUE(v.owed);
    ASSERT_EQ(v.missing.size(), (size_t)2);   // store + wflog
}

TEST("ritual: change + both required done -> satisfied") {
    RitualState s; s.changed = true;
    s.done = {RitualStep::Store, RitualStep::Wflog};
    auto v = evaluateRitual(s);
    ASSERT_FALSE(v.owed);
    ASSERT_TRUE(v.missing.empty());
}

TEST("ritual: advisory steps (graph/zone/verify) do NOT satisfy required") {
    RitualState s; s.changed = true;
    s.done = {RitualStep::Graph, RitualStep::Zone, RitualStep::Verify};
    auto v = evaluateRitual(s);
    ASSERT_TRUE(v.owed);
    ASSERT_EQ(v.missing.size(), (size_t)2);   // still owes store + wflog
}

TEST("ritual: partial -> only the missing required step reported") {
    RitualState s; s.changed = true;
    s.done = {RitualStep::Store};
    auto v = evaluateRitual(s);
    ASSERT_TRUE(v.owed);
    ASSERT_EQ(v.missing.size(), (size_t)1);
    ASSERT_EQ((int)v.missing[0], (int)RitualStep::Wflog);
}

TEST("ritual: stepForCommand maps the five syncs") {
    ASSERT_TRUE(stepForCommand("graph", "update").has_value());
    ASSERT_EQ((int)stepForCommand("graph", "update").value(), (int)RitualStep::Graph);
    ASSERT_EQ((int)stepForCommand("store", "").value(),        (int)RitualStep::Store);
    ASSERT_EQ((int)stepForCommand("memoir", "add").value(),    (int)RitualStep::Store);
    ASSERT_EQ((int)stepForCommand("known-issue", "add").value(),(int)RitualStep::Store);
    ASSERT_EQ((int)stepForCommand("wflog", "save").value(),    (int)RitualStep::Wflog);
    ASSERT_EQ((int)stepForCommand("verify", "").value(),       (int)RitualStep::Verify);
    ASSERT_EQ((int)stepForCommand("zone", "add").value(),      (int)RitualStep::Zone);
}

TEST("ritual: stepForCommand ignores ritual-neutral commands") {
    ASSERT_FALSE(stepForCommand("context", "foo.cpp").has_value());
    ASSERT_FALSE(stepForCommand("recall", "q").has_value());
    ASSERT_FALSE(stepForCommand("graph", "symbol").has_value());  // not "update"
}

TEST("ritual: stepName round-trips the labels") {
    ASSERT_EQ(ritualStepName(RitualStep::Graph),  std::string("graph-update"));
    ASSERT_EQ(ritualStepName(RitualStep::Store),  std::string("store"));
    ASSERT_EQ(ritualStepName(RitualStep::Wflog),  std::string("wflog"));
    ASSERT_EQ(ritualStepName(RitualStep::Verify), std::string("verify"));
    ASSERT_EQ(ritualStepName(RitualStep::Zone),   std::string("zone"));
}
