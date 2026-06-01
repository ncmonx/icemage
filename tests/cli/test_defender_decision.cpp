// v1.75.0 (#187): Defender exclusion should be idempotent + opt-out-able.
// `icmg update --apply` re-ran Add-MpPreference on EVERY upgrade, poking
// Defender into a full-volume scan that opens unrelated drives (subst B:).
// shouldRunDefenderExclusion gates that call: skip when already excluded
// (flag present) or when the user opted out (env / --no-defender).

#include "../test_main.hpp"
#include "../../src/cli/defender_decision.hpp"

using icmg::cli::shouldRunDefenderExclusion;

TEST("defender: first run, no flag, no opt-out -> run") {
    ASSERT_TRUE(shouldRunDefenderExclusion(/*flag_exists*/false,
                                           /*env_no_defender*/false,
                                           /*arg_no_defender*/false));
}

TEST("defender: flag already present -> skip (idempotent, fixes B: popup)") {
    ASSERT_FALSE(shouldRunDefenderExclusion(true, false, false));
}

TEST("defender: ICMG_NO_DEFENDER env -> skip even on first run") {
    ASSERT_FALSE(shouldRunDefenderExclusion(false, true, false));
}

TEST("defender: --no-defender flag -> skip even on first run") {
    ASSERT_FALSE(shouldRunDefenderExclusion(false, false, true));
}

TEST("defender: opt-out wins over everything") {
    ASSERT_FALSE(shouldRunDefenderExclusion(true, true, true));
}
