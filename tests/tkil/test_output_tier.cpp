// tests/tkil/test_output_tier.cpp
// TDD: failing tests for Auto-tier (icmg run). Write tests FIRST.
// Composition invariant (advisor): classify() consumes FULL output;
// delta is a sub-renderer of SUCCESS only. See docs/plans/extreme-token-saving.md

#include "../test_main.hpp"
#include "../../src/tkil/output_tier.hpp"

using icmg::tkil::OutputTier;
using icmg::tkil::classifyTier;
using icmg::tkil::renderWarningSummary;

// ---------------------------------------------------------------------------
// INVARIANT 1: warn-keyword in an unchanged line → WARNING (never SUCCESS)
// ---------------------------------------------------------------------------
TEST("tier: warning keyword → WARNING tier") {
    std::string out = "Building foo\nwarning: unused variable x\nDone\n";
    ASSERT_TRUE(classifyTier(0, out) == OutputTier::Warning);
}

// ---------------------------------------------------------------------------
// INVARIANT 2: success unchanged path classifies as SUCCESS
// ---------------------------------------------------------------------------
TEST("tier: clean success output → SUCCESS tier") {
    std::string out = "Building foo\nCompiled 12 files\nDone\n";
    ASSERT_TRUE(classifyTier(0, out) == OutputTier::Success);
}

// ---------------------------------------------------------------------------
// INVARIANT 3: error keyword forces ERROR even with exit 0
// ---------------------------------------------------------------------------
TEST("tier: error keyword with exit 0 → ERROR tier") {
    std::string out = "Building foo\nerror[E0502]: cannot borrow\n";
    ASSERT_TRUE(classifyTier(0, out) == OutputTier::Error);
}

// ---------------------------------------------------------------------------
// T4: exit non-zero → ERROR regardless of content
// ---------------------------------------------------------------------------
TEST("tier: exit non-zero → ERROR tier") {
    std::string out = "all good\n";  // benign content
    ASSERT_TRUE(classifyTier(1, out) == OutputTier::Error);
}

// ---------------------------------------------------------------------------
// T5: error keyword wins over warning keyword (error precedence)
// ---------------------------------------------------------------------------
TEST("tier: error precedence over warning") {
    std::string out = "warning: deprecated API\nerror: build failed\n";
    ASSERT_TRUE(classifyTier(0, out) == OutputTier::Error);
}

// ---------------------------------------------------------------------------
// T6: case-insensitive keyword matching
// ---------------------------------------------------------------------------
TEST("tier: case-insensitive ERROR/WARNING detection") {
    ASSERT_TRUE(classifyTier(0, "FATAL: oom\n") == OutputTier::Error);
    ASSERT_TRUE(classifyTier(0, "DEPRECATED: use bar\n") == OutputTier::Warning);
    ASSERT_TRUE(classifyTier(0, "Compilation OK\n") == OutputTier::Success);
}

// ---------------------------------------------------------------------------
// T7: renderWarningSummary shows head + count of remaining
// ---------------------------------------------------------------------------
TEST("tier: warning summary shows head N lines + remaining count") {
    std::string out = "line1\nline2\nline3\nline4\nline5\n";
    auto s = renderWarningSummary(out, /*head_lines=*/3);
    ASSERT_EQ(s.total_lines, 5);
    ASSERT_EQ(s.shown_lines, 3);
    ASSERT_TRUE(s.text.find("line1") != std::string::npos);
    ASSERT_TRUE(s.text.find("line3") != std::string::npos);
    ASSERT_TRUE(s.text.find("line4") == std::string::npos); // not in head
    ASSERT_TRUE(s.text.find("2 more line(s)") != std::string::npos);
}

// ---------------------------------------------------------------------------
// T8: warning summary with fewer lines than head shows all, no "more" tail
// ---------------------------------------------------------------------------
TEST("tier: warning summary with few lines shows all, no tail") {
    std::string out = "only1\nonly2\n";
    auto s = renderWarningSummary(out, /*head_lines=*/3);
    ASSERT_EQ(s.total_lines, 2);
    ASSERT_EQ(s.shown_lines, 2);
    ASSERT_TRUE(s.text.find("more line") == std::string::npos);
}

// ---------------------------------------------------------------------------
// T9: empty output → SUCCESS (no keywords, exit 0)
// ---------------------------------------------------------------------------
TEST("tier: empty output exit 0 → SUCCESS") {
    ASSERT_TRUE(classifyTier(0, "") == OutputTier::Success);
}
