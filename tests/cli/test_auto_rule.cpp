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

#include <cstdlib>

TEST("auto_rule: size_gt_500 returns NONE") {
    std::string big(600, 'a');
    auto r = detectNL(big);
    ASSERT_EQ((int)r.action, (int)NLAction::NONE);
}

TEST("auto_rule: priority_remove_before_add") {
    auto r = detectNL("hapus rule selalu-foo");
    ASSERT_EQ((int)r.action, (int)NLAction::REMOVE_RULE);
    ASSERT_EQ(r.target_name, std::string("selalu-foo"));
}

TEST("auto_rule: env_optout ICMG_NO_AUTO_RULE") {
#ifdef _WIN32
    _putenv_s("ICMG_NO_AUTO_RULE", "1");
#else
    setenv("ICMG_NO_AUTO_RULE", "1", 1);
#endif
    auto r = detectNL("ingat ya selalu pakai icmg");
    ASSERT_EQ((int)r.action, (int)NLAction::NONE);
#ifdef _WIN32
    _putenv_s("ICMG_NO_AUTO_RULE", "");
#else
    unsetenv("ICMG_NO_AUTO_RULE");
#endif
}

// fuzzyFind tests
static std::vector<RuleRecord> sample_corpus() {
    return {
        {"42", "no-emoji",     "do not use emoji in code or commits"},
        {"43", "always-icmg",  "always use icmg run for noisy commands"},
        {"44", "max-file-500", "do not read files larger than 500 lines"},
    };
}

TEST("fuzzy_exact") {
    auto res = fuzzyFind("no-emoji", sample_corpus(), 0.5);
    ASSERT_TRUE(!res.empty());
    ASSERT_EQ(res.front().id, std::string("42"));
    ASSERT_TRUE(res.front().score >= 0.9);
}

TEST("fuzzy_partial") {
    auto res = fuzzyFind("emoji", sample_corpus(), 0.3);
    ASSERT_TRUE(!res.empty());
    ASSERT_EQ(res.front().id, std::string("42"));
}

TEST("fuzzy_below_threshold") {
    auto res = fuzzyFind("xyz-nonexistent", sample_corpus(), 0.5);
    ASSERT_TRUE(res.empty());
}

// 20/20

// --- handleNL dispatcher tests (24/24) ---

TEST("handleNL_add_calls_saver") {
    std::string captured_name, captured_body;
    bool captured_update = true;
    NLAdapters a;
    a.rule_save = [&](const std::string& name, const std::string& body, bool update) -> int {
        captured_name = name;
        captured_body = body;
        captured_update = update;
        return 0;
    };
    auto msg = handleNL("ingat ya jangan buka file lebih dari 500 baris", a);
    ASSERT_TRUE(!captured_body.empty());
    ASSERT_TRUE(captured_update == false);
    ASSERT_TRUE(msg.find("auto-rule") != std::string::npos);
}

TEST("handleNL_remove_calls_disabler_soft") {
    std::string captured_id;
    NLAdapters a;
    a.rule_list = []() -> std::vector<RuleRecord> {
        return {{"42", "no-emoji", "do not use emoji"}};
    };
    a.rule_disable = [&](const std::string& id) -> int {
        captured_id = id;
        return 0;
    };
    auto msg = handleNL("hapus rule no-emoji", a);
    ASSERT_EQ(captured_id, std::string("42"));
    ASSERT_TRUE(msg.find("disabled") != std::string::npos);
    ASSERT_TRUE(msg.find("removed") == std::string::npos);
}

TEST("handleNL_remove_no_match") {
    NLAdapters a;
    a.rule_list = []() -> std::vector<RuleRecord> { return {}; };
    a.rule_disable = [](const std::string&) -> int { return 0; };
    auto msg = handleNL("hapus rule unknown-name", a);
    ASSERT_TRUE(msg.find("no rule matching") != std::string::npos);
}

TEST("handleNL_none_returns_empty") {
    NLAdapters a;
    auto msg = handleNL("just chatting normally here", a);
    ASSERT_EQ(msg, std::string(""));
}

// 24/24

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
