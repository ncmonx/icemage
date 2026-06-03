// Named composite flows: built-in registry + arg substitution. Pure data, so the
// chain definitions and the {ARG} placeholder logic are pinned independent of exec.
#include "../test_main.hpp"
#include "../../src/core/flow_registry.hpp"
#include <string>
using namespace icmg::core;

TEST("flow: built-in registry is non-empty") {
    ASSERT_TRUE(!builtinFlows().empty());
}

TEST("flow: findFlow returns a known flow with steps") {
    const Flow* f = findFlow("change-done");
    ASSERT_TRUE(f != nullptr);
    ASSERT_TRUE(!f->steps.empty());
}

TEST("flow: findFlow returns nullptr for unknown name") {
    ASSERT_TRUE(findFlow("no-such-flow") == nullptr);
}

TEST("flow: every step starts with a non-empty command token") {
    for (const auto& f : builtinFlows()) {
        ASSERT_TRUE(!f.steps.empty());
        for (const auto& step : f.steps) {
            ASSERT_TRUE(!step.empty());
            ASSERT_TRUE(!step[0].empty());
        }
    }
}

TEST("flow: substituteArg replaces {ARG} with the trailing argument") {
    const Flow* f = findFlow("change-done");
    ASSERT_TRUE(f != nullptr);
    auto sub = substituteArg(f->steps, "fixed the parser");
    bool found = false;
    for (const auto& step : sub)
        for (const auto& tok : step) {
            ASSERT_TRUE(tok != std::string("{ARG}"));   // no placeholder survives
            if (tok == "fixed the parser") found = true;
        }
    ASSERT_TRUE(found);
}

TEST("flow: flowNeedsArg is true for a flow with {ARG}, false otherwise") {
    const Flow* needsArg = findFlow("change-done");   // has wflog add {ARG}
    const Flow* noArg = findFlow("sanity");           // doctor + health, no {ARG}
    ASSERT_TRUE(needsArg != nullptr);
    ASSERT_TRUE(noArg != nullptr);
    ASSERT_TRUE(flowNeedsArg(*needsArg));
    ASSERT_TRUE(!flowNeedsArg(*noArg));
}

TEST("flow: substituteArg leaves arg-free flows unchanged") {
    const Flow* f = findFlow("sanity");
    ASSERT_TRUE(f != nullptr);
    auto sub = substituteArg(f->steps, "ignored");
    ASSERT_EQ(sub.size(), f->steps.size());
    ASSERT_EQ(sub[0][0], f->steps[0][0]);
}
