// 2026-06-06: exec_context premium-availability signal (no-premium LLM routing core).
// Local LLM should fire only when no premium (Claude) is present in this execution.

#include "../test_main.hpp"
#include "../../src/core/exec_context.hpp"

using namespace icmg::core;

TEST("exec_context: default is interactive + premium present") {
    setRunMode(RunMode::INTERACTIVE);   // reset any prior state
    setPremiumAvailable(true);
    ASSERT_TRUE(premiumAvailable());
    ASSERT_FALSE(isHeadless());
}

TEST("exec_context: setPremiumAvailable(false) reflected") {
    setPremiumAvailable(false);
    ASSERT_FALSE(premiumAvailable());
    setPremiumAvailable(true); // restore
}

TEST("exec_context: setRunMode HEADLESS reported by isHeadless") {
    setRunMode(RunMode::HEADLESS);
    ASSERT_TRUE(isHeadless());
    setRunMode(RunMode::INTERACTIVE);
}

TEST("exec_context: parseRunMode maps env strings") {
    ASSERT_TRUE(parseRunMode("headless") == RunMode::HEADLESS);
    ASSERT_TRUE(parseRunMode("interactive") == RunMode::INTERACTIVE);
    ASSERT_TRUE(parseRunMode("garbage") == RunMode::INTERACTIVE); // safe default
    ASSERT_TRUE(parseRunMode("") == RunMode::INTERACTIVE);
}

TEST("exec_context: env ICMG_RUN_MODE=headless => premium absent (fresh derive)") {
    // simulate child spawned by cron: setRunMode re-derives premium
    setRunMode(RunMode::HEADLESS);
    ASSERT_FALSE(premiumAvailable());
    setRunMode(RunMode::INTERACTIVE); // restore
}
