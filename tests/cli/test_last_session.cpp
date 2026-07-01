// `icmg recall --last-session` pure helpers (A2, 2026-07-01).
// Spec for last_session.hpp: session-topic classifier + wflog field extractor
// + last-session briefing renderer. No DB (query lives in recall_cmd.cpp).
#include "../test_main.hpp"
#include "../../src/cli/last_session.hpp"

using namespace icmg::cli;

// --- session-topic classifier ---

TEST("last-session: isSessionTopic matches the reserved prefixes") {
    ASSERT_TRUE(isSessionTopic("session-snapshot auto-precompact-20260701-101600"));
    ASSERT_TRUE(isSessionTopic("auto-compact-20260701-1016"));
    ASSERT_TRUE(isSessionTopic("session:mulai-beb-desain-tampilan"));
    ASSERT_TRUE(isSessionTopic("session foo"));
}

TEST("last-session: isSessionTopic rejects non-session topics") {
    ASSERT_FALSE(isSessionTopic("decisions-badge"));
    ASSERT_FALSE(isSessionTopic("log-saved ship badge"));
    ASSERT_FALSE(isSessionTopic("memoir:whatever"));
    ASSERT_FALSE(isSessionTopic(""));
}

// --- wflog field extractor ---

TEST("last-session: wflogField extracts Goal/Decisions/Open") {
    std::string c = "Goal: ship A2\nDecisions: extend recall\nRejected: new cmd\nOpen: wire wake-up";
    ASSERT_EQ(wflogField(c, "Goal"), std::string("ship A2"));
    ASSERT_EQ(wflogField(c, "Decisions"), std::string("extend recall"));
    ASSERT_EQ(wflogField(c, "Open"), std::string("wire wake-up"));
}

TEST("last-session: wflogField returns empty when key missing") {
    std::string c = "Goal: only goal here";
    ASSERT_EQ(wflogField(c, "Open"), std::string(""));
    ASSERT_EQ(wflogField(c, "Nonexistent"), std::string(""));
}

TEST("last-session: wflogField trims trailing whitespace/newline") {
    std::string c = "Goal: trimmed \nDecisions: x";
    ASSERT_EQ(wflogField(c, "Goal"), std::string("trimmed"));
}

// --- renderer ---

TEST("last-session: render with snapshot + wflog highlights Open") {
    SessionView v;
    v.has_snapshot = true;
    v.snap_topic = "session-snapshot auto-precompact-20260701";
    v.snap_content = "Recent queries: badge, tiers, D5 premise";
    v.snap_age = "2h ago";
    v.has_wflog = true;
    v.log_goal = "ship A2 recall --last-session";
    v.log_decisions = "extend recall, TDD";
    v.log_open = "wire into wake-up";
    v.log_age = "1h ago";
    std::string s = renderLastSession(v);
    ASSERT_TRUE(s.find("session-snapshot auto-precompact-20260701") != std::string::npos);
    ASSERT_TRUE(s.find("2h ago") != std::string::npos);
    ASSERT_TRUE(s.find("ship A2 recall") != std::string::npos);
    // Open items must be visibly flagged ("dimana kamu ketinggalan").
    ASSERT_TRUE(s.find("Open") != std::string::npos);
    ASSERT_TRUE(s.find("wire into wake-up") != std::string::npos);
}

TEST("last-session: render snapshot only (no wflog)") {
    SessionView v;
    v.has_snapshot = true;
    v.snap_topic = "auto-compact-20260701";
    v.snap_content = "some summary";
    v.snap_age = "5m ago";
    std::string s = renderLastSession(v);
    ASSERT_TRUE(s.find("auto-compact-20260701") != std::string::npos);
    ASSERT_TRUE(s.find("some summary") != std::string::npos);
}

TEST("last-session: render empty -> friendly no-data message") {
    SessionView v;  // nothing set
    std::string s = renderLastSession(v);
    ASSERT_TRUE(s.find("No prior session") != std::string::npos);
}

TEST("last-session: long snapshot content is capped") {
    SessionView v;
    v.has_snapshot = true;
    v.snap_topic = "auto-compact-x";
    v.snap_content = std::string(600, 'A');  // oversized dump
    std::string s = renderLastSession(v);
    ASSERT_TRUE(s.find(" ...") != std::string::npos);   // truncation marker present
    ASSERT_TRUE(s.size() < 400);                         // whole briefing stays small
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
