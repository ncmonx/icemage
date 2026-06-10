// v1.78.1: sayless precedence resolver (renamed from caveman v1.66).

#include "../test_main.hpp"
#include "../../src/cli/sayless_resolve.hpp"

using namespace icmg::cli;

TEST("sayless: default OFF when nothing set") {
    auto s = resolveSayless(false, false, false, "", "");
    ASSERT_FALSE(s.on);
    ASSERT_EQ(s.source, std::string("none"));
}

TEST("sayless: global ON inherited when no project flags") {
    auto s = resolveSayless(false, false, true, "", "ultra");
    ASSERT_TRUE(s.on);
    ASSERT_EQ(s.source, std::string("global"));
    ASSERT_EQ(s.level, std::string("ultra"));
}

TEST("sayless: project ON overrides absent global") {
    auto s = resolveSayless(false, true, false, "full", "");
    ASSERT_TRUE(s.on);
    ASSERT_EQ(s.source, std::string("project"));
    ASSERT_EQ(s.level, std::string("full"));
}

TEST("sayless: project OFF marker overrides global ON (key feature)") {
    auto s = resolveSayless(true, false, true, "", "ultra");
    ASSERT_FALSE(s.on);
    ASSERT_EQ(s.source, std::string("project-off"));
}

TEST("sayless: project OFF wins even if project ON flag also present") {
    auto s = resolveSayless(true, true, true, "lite", "ultra");
    ASSERT_FALSE(s.on);
    ASSERT_EQ(s.source, std::string("project-off"));
}

TEST("sayless: project ON beats global ON (project level used)") {
    auto s = resolveSayless(false, true, true, "lite", "ultra");
    ASSERT_TRUE(s.on);
    ASSERT_EQ(s.source, std::string("project"));
    ASSERT_EQ(s.level, std::string("lite"));
}

TEST("sayless: empty level defaults to ultra") {
    auto s = resolveSayless(false, true, false, "", "");
    ASSERT_TRUE(s.on);
    ASSERT_EQ(s.level, std::string("ultra"));
}

TEST("sayless: hyper level supported (new in v1.78.1)") {
    auto s = resolveSayless(false, true, false, "hyper", "");
    ASSERT_TRUE(s.on);
    ASSERT_EQ(s.level, std::string("hyper"));
}

// v2.x split: RESPONSE and THINKING use the SAME pure resolver, each called
// with its own flag's existence bools — so the two toggles are independent.
TEST("sayless: response + thinking resolve independently") {
    // response ON (global), thinking OFF (nothing set)
    auto resp  = resolveSayless(false, false, true,  "", "ultra");
    auto think = resolveSayless(false, false, false, "", "");
    ASSERT_TRUE(resp.on);
    ASSERT_FALSE(think.on);
    // reversed: response OFF (project-off marker), thinking ON (project flag)
    auto resp2  = resolveSayless(true,  false, true, "", "ultra");
    auto think2 = resolveSayless(false, true,  false, "", "");
    ASSERT_FALSE(resp2.on);
    ASSERT_TRUE(think2.on);
    ASSERT_EQ(think2.source, std::string("project"));
}
