// tests/tkil/test_output_delta.cpp
// TDD: failing tests for Delta-only feature (icmg run).
// Write tests FIRST — all must be RED before implementation.
// See: docs/plans/extreme-token-saving.md §Delta-only

#include "../test_main.hpp"
#include "../../src/tkil/output_delta.hpp"

using icmg::tkil::computeOutputDelta;
using icmg::tkil::OutputDeltaResult;
using icmg::tkil::kDeltaOutputCapBytes;

// ---------------------------------------------------------------------------
// T1: identical output → identical=true, text empty
// ---------------------------------------------------------------------------
TEST("delta: identical output → identical=true, text empty") {
    std::string out = "line one\nline two\nline three\n";
    auto r = computeOutputDelta(out, out, /*exit_ok=*/true, /*sacred=*/false);
    ASSERT_TRUE(r.identical);
    ASSERT_TRUE(r.text.empty());
    ASSERT_EQ(r.added_lines, 0);
    ASSERT_EQ(r.removed_lines, 0);
}

// ---------------------------------------------------------------------------
// T2: new lines added → added_lines=N, text contains only new lines
// ---------------------------------------------------------------------------
TEST("delta: new lines added → added_lines correct, text has only new lines") {
    std::string prev = "line one\nline two\n";
    std::string cur  = "line one\nline two\nnew line A\nnew line B\n";
    auto r = computeOutputDelta(prev, cur, /*exit_ok=*/true, /*sacred=*/false);
    ASSERT_FALSE(r.identical);
    ASSERT_EQ(r.added_lines, 2);
    // text should contain both new lines
    ASSERT_TRUE(r.text.find("new line A") != std::string::npos);
    ASSERT_TRUE(r.text.find("new line B") != std::string::npos);
    // text should NOT contain unchanged lines
    ASSERT_TRUE(r.text.find("line one") == std::string::npos);
    ASSERT_TRUE(r.text.find("line two") == std::string::npos);
}

// ---------------------------------------------------------------------------
// T3: lines removed → removed_lines=M
// ---------------------------------------------------------------------------
TEST("delta: lines removed → removed_lines counted") {
    std::string prev = "line one\nline two\nline three\n";
    std::string cur  = "line one\n";
    auto r = computeOutputDelta(prev, cur, /*exit_ok=*/true, /*sacred=*/false);
    ASSERT_FALSE(r.identical);
    ASSERT_EQ(r.removed_lines, 2); // line two + line three gone
}

// ---------------------------------------------------------------------------
// T4: sacred lines (containing "error:") always emitted even if "unchanged"
// ---------------------------------------------------------------------------
TEST("delta: sacred line 'error:' always emitted even when in prev") {
    std::string prev = "Build ok\nerror: something bad\n";
    std::string cur  = "Build ok\nerror: something bad\n";
    // With sacred_always=true (default), sacred lines always force emit
    auto r = computeOutputDelta(prev, cur, /*exit_ok=*/true, /*sacred_always=*/true);
    ASSERT_FALSE(r.identical);
    ASSERT_TRUE(r.text.find("error: something bad") != std::string::npos);
}

TEST("delta: sacred disabled → identical output gives identical=true") {
    std::string prev = "Build ok\nerror: something bad\n";
    std::string cur  = "Build ok\nerror: something bad\n";
    auto r = computeOutputDelta(prev, cur, /*exit_ok=*/true, /*sacred_always=*/false);
    ASSERT_TRUE(r.identical);
}

// ---------------------------------------------------------------------------
// T5: exit non-zero → delta disabled, returns full output unchanged
// ---------------------------------------------------------------------------
TEST("delta: exit non-zero → delta disabled, full output returned") {
    std::string prev = "line one\nline two\n";
    std::string cur  = "line one\nline two\n";  // identical but exit failed
    auto r = computeOutputDelta(prev, cur, /*exit_ok=*/false);
    // Must not report identical — command failed, show everything
    ASSERT_FALSE(r.identical);
    ASSERT_EQ(r.text, cur);
    ASSERT_EQ(r.added_lines, 0);
}

// ---------------------------------------------------------------------------
// T6: first run (prev empty) → first_run=true, full cur output returned
// ---------------------------------------------------------------------------
TEST("delta: first run (empty prev) → first_run=true, returns full output") {
    std::string cur = "first run output\nline two\n";
    auto r = computeOutputDelta(/*prev=*/"", cur, /*exit_ok=*/true);
    ASSERT_TRUE(r.first_run);
    ASSERT_EQ(r.text, cur);
    ASSERT_FALSE(r.identical);
}

// ---------------------------------------------------------------------------
// T7: output > 64KB → hash_only=true, no text diff
// ---------------------------------------------------------------------------
TEST("delta: output > 64KB → hash_only=true") {
    // Build a string > kDeltaOutputCapBytes
    std::string big(kDeltaOutputCapBytes + 1, 'x');
    auto r = computeOutputDelta(big, big, /*exit_ok=*/true);
    ASSERT_TRUE(r.hash_only);
    ASSERT_TRUE(r.text.empty());
}

// ---------------------------------------------------------------------------
// T8: identical output with multiple diff line types → only new lines in text
// ---------------------------------------------------------------------------
TEST("delta: mixed add/remove → only added lines in text") {
    std::string prev = "aaa\nbbb\nccc\n";
    std::string cur  = "aaa\nccc\nddd\n";  // bbb removed, ddd added
    auto r = computeOutputDelta(prev, cur, /*exit_ok=*/true, /*sacred=*/false);
    ASSERT_FALSE(r.identical);
    ASSERT_EQ(r.added_lines, 1);   // ddd
    ASSERT_EQ(r.removed_lines, 1); // bbb
    ASSERT_TRUE(r.text.find("ddd") != std::string::npos);
    ASSERT_TRUE(r.text.find("bbb") == std::string::npos); // removed not in cur text
}

// ---------------------------------------------------------------------------
// T9: isSacredLine detects all keywords
// ---------------------------------------------------------------------------
TEST("isSacredLine: detects error/fatal/failed/assert/panic/segfault") {
    ASSERT_TRUE(icmg::tkil::isSacredLine("error[E0502]: borrow"));
    ASSERT_TRUE(icmg::tkil::isSacredLine("FATAL: out of memory"));
    ASSERT_TRUE(icmg::tkil::isSacredLine("3 tests FAILED"));
    ASSERT_TRUE(icmg::tkil::isSacredLine("ASSERT_EQ(a, b) failed"));
    ASSERT_TRUE(icmg::tkil::isSacredLine("thread 'main' panicked"));
    ASSERT_TRUE(icmg::tkil::isSacredLine("Segfault at 0x00"));
    ASSERT_FALSE(icmg::tkil::isSacredLine("all tests passed"));
    ASSERT_FALSE(icmg::tkil::isSacredLine("build ok"));
    ASSERT_FALSE(icmg::tkil::isSacredLine(""));
}

// ---------------------------------------------------------------------------
// T10: empty lines treated as unchanged (no noise from blank lines)
// ---------------------------------------------------------------------------
TEST("delta: empty lines treated as unchanged, not added") {
    std::string prev = "aaa\n\nbbb\n";
    std::string cur  = "aaa\n\nbbb\n\n\n"; // only extra blank lines added
    auto r = computeOutputDelta(prev, cur, /*exit_ok=*/true, /*sacred=*/false);
    // Extra blanks should not count as "added"
    ASSERT_TRUE(r.identical);
    ASSERT_EQ(r.added_lines, 0);
}
