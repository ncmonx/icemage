#include "../test_main.hpp"
#include "../../src/cli/think_directive.hpp"

using icmg::cli::Intent;
using icmg::cli::classifyIntent;
using icmg::cli::applyNoThinkDirective;
using icmg::cli::applyConciseDirective;
using icmg::cli::hasDirective;
using icmg::cli::intentLabel;

TEST("classify: rename → simple") {
    ASSERT_EQ(intentLabel(classifyIntent("rename foo.ts to bar.ts")), std::string("simple"));
}

TEST("classify: short prompt → simple") {
    ASSERT_EQ(intentLabel(classifyIntent("show users")), std::string("simple"));
}

TEST("classify: debug bug → complex") {
    ASSERT_EQ(intentLabel(classifyIntent("debug race condition in auth middleware")),
              std::string("complex"));
}

TEST("classify: long prompt no keyword → complex") {
    auto i = classifyIntent("this is a fairly long question about how things work in the system");
    ASSERT_EQ(intentLabel(i), std::string("complex"));
}

TEST("classify: medium length unknown") {
    auto i = classifyIntent("update the timestamp field on user record entry");
    ASSERT_EQ(intentLabel(i), std::string("unknown"));
}

TEST("classify: complex wins over simple on tie") {
    auto i = classifyIntent("show me how to debug this race condition");
    ASSERT_EQ(intentLabel(i), std::string("complex"));
}

// v2.x suppress-gate: more mechanical / status / ack classes classify as simple
// (so the skip-thinking hint fires on them). COMPLEX still wins on tie.
TEST("classify: commit + push mechanical → simple") {
    ASSERT_EQ(intentLabel(classifyIntent("commit these changes and push them upstream")),
              std::string("simple"));
}
TEST("classify: git status mid-sentence → simple") {
    ASSERT_EQ(intentLabel(classifyIntent("check the git status of the working tree")),
              std::string("simple"));
}
TEST("classify: Indonesian ack 'lanjut' → simple") {
    ASSERT_EQ(intentLabel(classifyIntent("ok lanjut yang tadi sampai semua hijau")),
              std::string("simple"));
}
TEST("classify: complex still wins over the new simple keywords") {
    ASSERT_EQ(intentLabel(classifyIntent("commit after you refactor the auth module")),
              std::string("complex"));
}

TEST("directive: no-think wraps + idempotent") {
    auto a = applyNoThinkDirective("task X");
    ASSERT_TRUE(hasDirective(a));
    ASSERT_TRUE(a.find("Answer directly") != std::string::npos);
    auto b = applyNoThinkDirective(a);
    ASSERT_EQ(a, b);
}

TEST("directive: concise stronger than no-think") {
    auto c = applyConciseDirective("task X");
    ASSERT_TRUE(hasDirective(c));
    ASSERT_TRUE(c.find("under 100 words") != std::string::npos);
}

TEST("directive: sayless ultra-terse fragment style") {
    auto c = icmg::cli::applySaylessDirective("task X");
    ASSERT_TRUE(hasDirective(c));
    ASSERT_TRUE(c.find("Sayless mode") != std::string::npos);
    ASSERT_TRUE(c.find("Drop articles") != std::string::npos);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
