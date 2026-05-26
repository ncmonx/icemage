// v1.51.0 TDD: auto_rule NL detection skeleton test.
#include "../test_main.hpp"
#include "../../src/cli/auto_rule.hpp"

using namespace icmg::cli;

TEST("auto_rule: short line returns NONE") {
    auto r = detectNL("hi");
    ASSERT_EQ((int)r.action, (int)NLAction::NONE);
}

TEST("auto_rule: ingat ya triggers ADD_RULE") {
    auto r = detectNL("ingat ya jangan buka file lebih dari 500 baris");
    ASSERT_EQ((int)r.action, (int)NLAction::ADD_RULE);
    ASSERT_TRUE(!r.content.empty());
}

TEST("auto_rule: selalu triggers ADD_RULE") {
    auto r = detectNL("selalu pakai icmg parallel kalau 2+ tugas");
    ASSERT_EQ((int)r.action, (int)NLAction::ADD_RULE);
}

TEST("auto_rule: remember triggers ADD_RULE") {
    auto r = detectNL("remember to always run tests before commit");
    ASSERT_EQ((int)r.action, (int)NLAction::ADD_RULE);
}

TEST("auto_rule: rule colon triggers ADD_RULE") {
    auto r = detectNL("rule: never use git push --force on main");
    ASSERT_EQ((int)r.action, (int)NLAction::ADD_RULE);
}

TEST("auto_rule: mid sentence selalu skipped") {
    auto r = detectNL("kemarin saya selalu lapar di pagi hari");
    ASSERT_EQ((int)r.action, (int)NLAction::NONE);
}

TEST("auto_rule: hapus rule triggers REMOVE_RULE") {
    auto r = detectNL("hapus rule no-emoji");
    ASSERT_EQ((int)r.action, (int)NLAction::REMOVE_RULE);
    ASSERT_EQ(r.target_name, std::string("no-emoji"));
}

TEST("auto_rule: delete rule triggers REMOVE_RULE") {
    auto r = detectNL("delete rule foo-bar");
    ASSERT_EQ((int)r.action, (int)NLAction::REMOVE_RULE);
    ASSERT_EQ(r.target_name, std::string("foo-bar"));
}

TEST("auto_rule: ubah rule triggers EDIT_RULE") {
    auto r = detectNL("ubah rule foo jadi bar baz qux");
    ASSERT_EQ((int)r.action, (int)NLAction::EDIT_RULE);
    ASSERT_EQ(r.target_name, std::string("foo"));
    ASSERT_EQ(r.content, std::string("bar baz qux"));
}

TEST("auto_rule: change rule triggers EDIT_RULE") {
    auto r = detectNL("change rule foo to new body here");
    ASSERT_EQ((int)r.action, (int)NLAction::EDIT_RULE);
    ASSERT_EQ(r.target_name, std::string("foo"));
    ASSERT_EQ(r.content, std::string("new body here"));
}

TEST("auto_rule: tambah skill with body triggers ADD_SKILL") {
    auto r = detectNL("tambah skill linter clang-format runner");
    ASSERT_EQ((int)r.action, (int)NLAction::ADD_SKILL);
    ASSERT_EQ(r.target_name, std::string("linter"));
    ASSERT_EQ(r.content, std::string("clang-format runner"));
}

TEST("auto_rule: create skill with body triggers ADD_SKILL") {
    auto r = detectNL("create skill formatter prettier on save");
    ASSERT_EQ((int)r.action, (int)NLAction::ADD_SKILL);
    ASSERT_EQ(r.target_name, std::string("formatter"));
}

TEST("auto_rule: delete skill triggers REMOVE_SKILL") {
    auto r = detectNL("delete skill linter");
    ASSERT_EQ((int)r.action, (int)NLAction::REMOVE_SKILL);
    ASSERT_EQ(r.target_name, std::string("linter"));
}

TEST("auto_rule: tambah skill name-only returns NONE") {
    auto r = detectNL("tambah skill foo");
    ASSERT_EQ((int)r.action, (int)NLAction::NONE);
}

// 14/14

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
