// TDD (2026-06-15): smart-install version caching decision logic.
// Spec: claude-mem smart-install idea — skip the binary copy when the system
// install is already at the running version, so repeated `icmg install --system`
// is a cheap no-op. Pure decision helper so the policy is unit-testable.
// Failing FIRST: src/cli/install_version.hpp does not exist yet.

#include "../test_main.hpp"
#include "../../src/cli/install_version.hpp"

#include <string>

using icmg::cli::shouldReinstall;
using icmg::cli::InstallDecision;

// 1. No recorded version -> must install (fresh).
TEST("install-version: no installed version -> reinstall") {
    InstallDecision d = shouldReinstall("2.4.2", "", /*force*/false);
    ASSERT_TRUE(d.reinstall);
    ASSERT_CONTAINS(d.reason, "no install");
}

// 2. Same version, no force -> skip (cached).
TEST("install-version: same version, no force -> skip") {
    InstallDecision d = shouldReinstall("2.4.2", "2.4.2", false);
    ASSERT_FALSE(d.reinstall);
    ASSERT_CONTAINS(d.reason, "up to date");
}

// 3. Different version -> reinstall (upgrade/downgrade).
TEST("install-version: version mismatch -> reinstall") {
    InstallDecision d = shouldReinstall("2.4.3", "2.4.2", false);
    ASSERT_TRUE(d.reinstall);
    ASSERT_CONTAINS(d.reason, "2.4.2");   // mentions old version
    ASSERT_CONTAINS(d.reason, "2.4.3");   // mentions new version
}

// 4. Same version but --force -> reinstall anyway.
TEST("install-version: force overrides cache") {
    InstallDecision d = shouldReinstall("2.4.2", "2.4.2", /*force*/true);
    ASSERT_TRUE(d.reinstall);
    ASSERT_CONTAINS(d.reason, "force");
}
