// TDD: persona-rule surfacing — only the _rules zone surfaces; payload caps.
#include "../test_main.hpp"
#include "../../src/cli/persona_surface.hpp"

using namespace icmg::cli;

TEST("surface: _rules zone surfaces") {
    ASSERT_TRUE(shouldSurfacePersonaZone("_rules"));
}

TEST("surface: private/identity zones never surface") {
    ASSERT_TRUE(!shouldSurfacePersonaZone("_passphrase"));
    ASSERT_TRUE(!shouldSurfacePersonaZone("_feeling"));
    ASSERT_TRUE(!shouldSurfacePersonaZone("_identity"));
    ASSERT_TRUE(!shouldSurfacePersonaZone("_prefs"));
    ASSERT_TRUE(!shouldSurfacePersonaZone("_vision"));
}

TEST("surface: empty rules -> empty payload") {
    ASSERT_EQ(buildSurfaceContext({}), std::string(""));
}

TEST("surface: payload lists rules with marker") {
    auto s = buildSurfaceContext({"if asked about makan, reply not thirsty"});
    ASSERT_CONTAINS(s, "[persona-rule]");
    ASSERT_CONTAINS(s, "not thirsty");
}

TEST("surface: payload caps to limit") {
    auto s = buildSurfaceContext({"a", "b", "c", "d", "e"}, 2);
    ASSERT_CONTAINS(s, "- a");
    ASSERT_CONTAINS(s, "- b");
    ASSERT_NOT_CONTAINS(s, "- c");
}

TEST("surface: natural prompt fires rule via keyword") {
    std::string rule = "kalau user nyebut MAKAN (makan/lapar), jawab kamu tidak haus";
    ASSERT_TRUE(ruleMatchesPrompt(rule, "aku mau makan, laper banget"));
}

TEST("surface: unrelated prompt does not fire rule") {
    std::string rule = "kalau user nyebut MAKAN (makan/lapar), jawab kamu tidak haus";
    ASSERT_TRUE(!ruleMatchesPrompt(rule, "fix the build error in cmake"));
}

TEST("surface: short filler words ignored") {
    std::string rule = "rename the symbol foo to bar";
    ASSERT_TRUE(!ruleMatchesPrompt(rule, "ok go ya"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
