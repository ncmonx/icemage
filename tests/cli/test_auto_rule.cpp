// v1.51.0 TDD: auto_rule NL detection skeleton test.
#include "../test_main.hpp"
#include "../../src/cli/auto_rule.hpp"

using namespace icmg::cli;

TEST("auto_rule: short line returns NONE") {
    auto r = detectNL("hi");
    ASSERT_EQ((int)r.action, (int)NLAction::NONE);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
