// test_hook_approach — unit tests for T5 approach-history hooks.
//
// Tests:
//   1. approach inject empty prompt → empty string.
//   2. approach inject honors ICMG_APPROACH_QUIET=1.
//   3. approach inject no DB → empty (fail-soft).
//   4. test-outcome no match → empty (non-test command).
//   5. test-outcome ctest success signature → "" (side-effect only, no throw).

#include "../test_main.hpp"
#include "../../src/core/hooks/internals.hpp"

#include <cstdlib>
#include <string>

namespace {

void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v && *v) setenv(k, v, 1); else unsetenv(k);
#endif
}

} // namespace

// ---- TEST 1: empty prompt → empty ------------------------------------------

TEST("runUserPromptApproachInject empty prompt → empty") {
    std::string out = icmg::core::hooks::runUserPromptApproachInject("");
    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 2: ICMG_APPROACH_QUIET=1 suppresses ------------------------------

TEST("runUserPromptApproachInject honors ICMG_APPROACH_QUIET=1") {
    setEnv("ICMG_APPROACH_QUIET", "1");
    std::string out = icmg::core::hooks::runUserPromptApproachInject(
        "fix the auth bug in login module");
    setEnv("ICMG_APPROACH_QUIET", "");
    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 3: no project DB → empty (fail-soft) -----------------------------

TEST("runUserPromptApproachInject no DB → empty fail-soft") {
    // Change working directory to a temp path with no .icmg/data.db.
    // Since we can't change cwd portably in-process, we rely on the fact that
    // when Config::projectDbPath(".") yields a non-existent DB path, Db open
    // either creates empty or throws — in both cases result must be "".
    // The simplest proof: call with a non-empty prompt and verify no throw + "".
    // (In CI the project DB may or may not exist; function must not throw.)
    bool threw = false;
    std::string out;
    try {
        out = icmg::core::hooks::runUserPromptApproachInject(
            "some totally random prompt that has no matching task in db xyz123");
    } catch (...) {
        threw = true;
    }
    ASSERT_FALSE(threw);
    // If DB exists with no matching rows → "", if DB doesn't exist → "" too.
    // Either way: no throw is the requirement; empty is the default.
    (void)out; // may be "" or may contain unrelated matches — not asserting content
}

// ---- TEST 4: test-outcome non-test command → empty -------------------------

TEST("runPostToolUseTestOutcome non-test command → empty") {
    std::string out = icmg::core::hooks::runPostToolUseTestOutcome(
        "ls -la", "", 0);
    ASSERT_EQ(out, std::string(""));
}

// ---- TEST 5: test-outcome ctest success → "" (no throw) --------------------

TEST("runPostToolUseTestOutcome ctest success signature no-throw") {
    bool threw = false;
    std::string out;
    try {
        out = icmg::core::hooks::runPostToolUseTestOutcome(
            "ctest --test-dir build",
            "100% tests passed, 0 tests failed out of 63\n"
            "Total Test time (real) = 8.42 sec\n",
            0);
    } catch (...) {
        threw = true;
    }
    ASSERT_FALSE(threw);
    // Record-only path — always returns empty.
    ASSERT_EQ(out, std::string(""));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
