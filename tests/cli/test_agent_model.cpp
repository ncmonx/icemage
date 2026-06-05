// TDD: token routing — --light picks a cheap model, --model overrides.
#include "../test_main.hpp"
#include "../../src/cli/agent_model.hpp"

using icmg::cli::applyAgentModel;
using icmg::cli::kLightModel;

TEST("agent-model: default leaves command unchanged") {
    ASSERT_EQ(applyAgentModel("claude --print", false, ""),
              std::string("claude --print"));
}

TEST("agent-model: --light appends cheap tier") {
    ASSERT_EQ(applyAgentModel("claude --print", true, ""),
              std::string("claude --print --model ") + kLightModel);
}

TEST("agent-model: explicit override appended") {
    ASSERT_EQ(applyAgentModel("claude --print", false, "claude-opus-4-8"),
              std::string("claude --print --model claude-opus-4-8"));
}

TEST("agent-model: override beats --light") {
    ASSERT_EQ(applyAgentModel("x", true, "m"), std::string("x --model m"));
}

TEST("agent-model: --light skips when model already present") {
    ASSERT_EQ(applyAgentModel("claude --model foo", true, ""),
              std::string("claude --model foo"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
