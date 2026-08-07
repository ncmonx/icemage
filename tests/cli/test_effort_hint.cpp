#include "../test_main.hpp"
#include "../../src/cli/effort_hint.hpp"

using namespace icmg::cli;

TEST("effort: simple task low budget") {
    auto h = recommendEffort(Intent::Simple, 0);
    ASSERT_EQ((int)h.level, (int)EffortLevel::Low);
    ASSERT_EQ(h.budget_tokens, 2000);
}
TEST("effort: complex task high budget") {
    auto h = recommendEffort(Intent::Complex, 0);
    ASSERT_EQ((int)h.level, (int)EffortLevel::High);
    ASSERT_EQ(h.budget_tokens, 16000);
}
TEST("effort: unknown task medium budget") {
    auto h = recommendEffort(Intent::Unknown, 0);
    ASSERT_EQ((int)h.level, (int)EffortLevel::Medium);
    ASSERT_EQ(h.budget_tokens, 8000);
}
TEST("effort: multi-file scope bumps simple->medium") {
    auto h = recommendEffort(Intent::Simple, 10);
    ASSERT_EQ((int)h.level, (int)EffortLevel::Medium);
}
TEST("effort: wide blast radius forces high regardless of intent") {
    auto h = recommendEffort(Intent::Simple, 30);
    ASSERT_EQ((int)h.level, (int)EffortLevel::High);
    ASSERT_EQ(h.budget_tokens, 16000);
}
TEST("effort: small fan-out does not escalate") {
    auto h = recommendEffort(Intent::Simple, 5);
    ASSERT_EQ((int)h.level, (int)EffortLevel::Low);
}
TEST("effort directive: wraps, idempotent, carries budget") {
    auto h = recommendEffort(Intent::Complex, 0);
    auto once = applyEffortDirective("BODY", h);
    ASSERT_TRUE(once.find("<icmg-effort") != std::string::npos);
    ASSERT_TRUE(once.find("16000") != std::string::npos);
    ASSERT_TRUE(once.find("BODY") != std::string::npos);
    auto twice = applyEffortDirective(once, h);
    ASSERT_EQ(once, twice);   // idempotent
}
TEST("effort: estimateFanOut counts symbol markers") {
    std::string blob =
        "## Files & Symbols\n"
        "### Foo (fn, L1-9)\n"
        "### Bar (fn, L10-20)\n"
        "some body\n"
        "### Baz (class, L21-40)\n";
    ASSERT_EQ(estimateFanOut(blob), 3);
}
TEST("effort label round-trips") {
    ASSERT_TRUE(std::string(effortLabel(EffortLevel::Low)) == "low");
    ASSERT_TRUE(std::string(effortLabel(EffortLevel::High)) == "high");
}

// v2.21 research B: adaptive recall depth binding.
TEST("adaptive depth: simple shallow, complex deep, unknown middle") {
    ASSERT_EQ(adaptiveRecallDepth(Intent::Simple), 3);
    ASSERT_EQ(adaptiveRecallDepth(Intent::Complex), 12);
    ASSERT_EQ(adaptiveRecallDepth(Intent::Unknown), 7);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
