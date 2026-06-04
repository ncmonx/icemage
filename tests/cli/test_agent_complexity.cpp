// TDD: auto-route classifier — mechanical tasks route cheap, judgment stays full.
#include "../test_main.hpp"
#include "../../src/cli/agent_complexity.hpp"

using icmg::cli::isLightweightTask;

TEST("complexity: create-file is lightweight") {
    ASSERT_TRUE(isLightweightTask("Create a file at /tmp/x with content y"));
}

TEST("complexity: rename is lightweight") {
    ASSERT_TRUE(isLightweightTask("Rename foo to bar in the config"));
}

TEST("complexity: refactor is heavy") {
    ASSERT_TRUE(!isLightweightTask("Refactor the auth module and clean it up"));
}

TEST("complexity: debug/investigate is heavy") {
    ASSERT_TRUE(!isLightweightTask("Investigate why the build hangs"));
}

TEST("complexity: fix is heavy even with mechanical words") {
    // "fix" wins over "rename" — judgment work, keep full model.
    ASSERT_TRUE(!isLightweightTask("Fix the bug then rename the symbol"));
}

TEST("complexity: unknown task defaults to heavy (conservative)") {
    ASSERT_TRUE(!isLightweightTask("Make the thing work somehow"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
