// `icmg agent` persona policy: coding sub-agents stay clean; advisory is opt-in.
#include "../test_main.hpp"
#include "../../src/cli/agent_persona_policy.hpp"
using namespace icmg::cli;

TEST("agent-persona: --exec NEVER uses persona (even if config on)") {
    ASSERT_TRUE(!agentUsePersona(/*exec=*/true, /*no_env=*/false, /*cfg=*/true));
    ASSERT_TRUE(!agentUsePersona(/*exec=*/true, /*no_env=*/false, /*cfg=*/false));
}

TEST("agent-persona: advisory default OFF (config off)") {
    ASSERT_TRUE(!agentUsePersona(/*exec=*/false, /*no_env=*/false, /*cfg=*/false));
}

TEST("agent-persona: advisory opt-in ON (config on)") {
    ASSERT_TRUE(agentUsePersona(/*exec=*/false, /*no_env=*/false, /*cfg=*/true));
}

TEST("agent-persona: ICMG_NO_PERSONA forces off even with config on") {
    ASSERT_TRUE(!agentUsePersona(/*exec=*/false, /*no_env=*/true, /*cfg=*/true));
}
