// v1.75.1: PreCompact hook must emit output Claude Code's schema accepts.
// The old emit used hookSpecificOutput.additionalContext, which PreCompact
// rejects -> output silently dropped every compaction.

#include "../test_main.hpp"
#include "../../src/core/hooks/precompact_output.hpp"

#include <nlohmann/json.hpp>

using icmg::core::hooks::preCompactOutputJson;
using icmg::core::hooks::isValidPreCompactOutput;

TEST("precompact: emitted output validates against allowed PreCompact schema") {
    ASSERT_TRUE(isValidPreCompactOutput(preCompactOutputJson()));
}

TEST("precompact: emitted output is a parseable object with only allowed keys") {
    auto j = nlohmann::json::parse(preCompactOutputJson());
    ASSERT_TRUE(j.is_object());
    ASSERT_TRUE(j.contains("suppressOutput"));
    ASSERT_FALSE(j.contains("hookSpecificOutput"));  // the bug shape
}

TEST("precompact: the OLD bad shape is rejected by the validator") {
    // Exactly what runPreCompactHook used to emit.
    std::string bad =
        R"({"hookSpecificOutput":{"hookEventName":"PreCompact",)"
        R"("additionalContext":"ABSOLUTE RULE..."}})";
    ASSERT_FALSE(isValidPreCompactOutput(bad));
}

TEST("precompact: empty output is valid (emit nothing)") {
    ASSERT_TRUE(isValidPreCompactOutput(""));
}

TEST("precompact: non-json garbage is invalid") {
    ASSERT_FALSE(isValidPreCompactOutput("not json"));
}
