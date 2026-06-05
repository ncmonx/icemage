// Tests for icmg::cli::assembleTask — flag/value-aware task-string assembly.
#include "../test_main.hpp"
#include "../../src/cli/agent_task.hpp"

using icmg::cli::assembleTask;

TEST("assembleTask skips --timeout value") {
    ASSERT_EQ(assembleTask({"do", "x", "--timeout", "300"}), std::string("do x"));
}

TEST("assembleTask skips boolean flags") {
    ASSERT_EQ(assembleTask({"make", "file", "--exec", "--no-store"}), std::string("make file"));
}

TEST("assembleTask skips --command value mid-args") {
    ASSERT_EQ(assembleTask({"a", "--command", "claudeprint", "b"}), std::string("a b"));
}

TEST("assembleTask empty when only flag+value") {
    ASSERT_EQ(assembleTask({"--timeout", "90"}), std::string(""));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
