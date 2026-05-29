// v1.66: per-project caveman precedence resolver.

#include "../test_main.hpp"
#include "../../src/cli/caveman_resolve.hpp"

using namespace icmg::cli;

TEST("caveman: default OFF when nothing set") {
    auto s = resolveCaveman(false, false, false, "", "");
    ASSERT_FALSE(s.on);
    ASSERT_EQ(s.source, std::string("none"));
}

TEST("caveman: global ON inherited when no project flags") {
    auto s = resolveCaveman(false, false, true, "", "ultra");
    ASSERT_TRUE(s.on);
    ASSERT_EQ(s.source, std::string("global"));
    ASSERT_EQ(s.level, std::string("ultra"));
}

TEST("caveman: project ON overrides absent global") {
    auto s = resolveCaveman(false, true, false, "full", "");
    ASSERT_TRUE(s.on);
    ASSERT_EQ(s.source, std::string("project"));
    ASSERT_EQ(s.level, std::string("full"));
}

TEST("caveman: project OFF marker overrides global ON (key feature)") {
    auto s = resolveCaveman(true, false, true, "", "ultra");
    ASSERT_FALSE(s.on);
    ASSERT_EQ(s.source, std::string("project-off"));
}

TEST("caveman: project OFF wins even if project ON flag also present") {
    auto s = resolveCaveman(true, true, true, "lite", "ultra");
    ASSERT_FALSE(s.on);
    ASSERT_EQ(s.source, std::string("project-off"));
}

TEST("caveman: project ON beats global ON (project level used)") {
    auto s = resolveCaveman(false, true, true, "lite", "ultra");
    ASSERT_TRUE(s.on);
    ASSERT_EQ(s.source, std::string("project"));
    ASSERT_EQ(s.level, std::string("lite"));
}

TEST("caveman: empty level defaults to ultra") {
    auto s = resolveCaveman(false, true, false, "", "");
    ASSERT_TRUE(s.on);
    ASSERT_EQ(s.level, std::string("ultra"));
}
