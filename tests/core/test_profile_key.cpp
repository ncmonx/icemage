// Pure normalization for the zoned profile store: zone+key -> canonical form.
#include "../test_main.hpp"
#include "../../src/core/profile_key.hpp"
#include <string>
using namespace icmg::core;

TEST("normalizeZone: lowercases + trims + default on empty") {
    ASSERT_EQ(normalizeZone("  Work Notes "), std::string("work-notes"));
    ASSERT_EQ(normalizeZone(""), std::string("default"));
}

TEST("normalizeKey: lowercases + collapses to [a-z0-9_-]") {
    ASSERT_EQ(normalizeKey("My Skill #1!"), std::string("my-skill-1"));
}

TEST("normalizeKey: empty -> empty (caller rejects)") {
    ASSERT_EQ(normalizeKey("   "), std::string(""));
}

TEST("validKind: known kinds only, default profile") {
    ASSERT_EQ(validKind("skill"), std::string("skill"));
    ASSERT_EQ(validKind("note"),  std::string("note"));
    ASSERT_EQ(validKind("xyz"),   std::string("profile"));  // unknown -> profile
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
