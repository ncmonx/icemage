#include "../test_main.hpp"
#include "../../src/tkil/filters/filter_utils.hpp"

using namespace icmg::tkil;

// ---- filter_utils unit tests -----------------------------------------------

TEST("splitLines: basic") {
    auto lines = splitLines("a\nb\nc");
    ASSERT_EQ(lines.size(), 3u);
    ASSERT_EQ(lines[0], std::string("a"));
    ASSERT_EQ(lines[2], std::string("c"));
}

TEST("splitLines: CRLF stripped") {
    auto lines = splitLines("a\r\nb\r\n");
    ASSERT_EQ(lines[0], std::string("a"));
    ASSERT_EQ(lines[1], std::string("b"));
}

TEST("splitLines: empty string") {
    auto lines = splitLines("");
    ASSERT_TRUE(lines.empty());
}

TEST("splitLines: single line no newline") {
    auto lines = splitLines("hello");
    ASSERT_EQ(lines.size(), 1u);
    ASSERT_EQ(lines[0], std::string("hello"));
}

TEST("containsAny: matches keyword") {
    ASSERT_TRUE(containsAny("error: something went wrong", {"error", "warning"}));
}

TEST("containsAny: case-insensitive") {
    ASSERT_TRUE(containsAny("ERROR: bad", {"error"}));
}

TEST("containsAny: no match") {
    ASSERT_FALSE(containsAny("info: all good", {"error", "warning"}));
}

TEST("containsAny: empty keywords") {
    ASSERT_FALSE(containsAny("anything", {}));
}

TEST("applyHardLimit: under limit — no truncation") {
    FilterResult fr;
    fr.output         = "line1\nline2\n";
    fr.original_lines = 2;
    fr.filtered_lines = 2;
    fr.was_truncated  = false;

    auto result = applyHardLimit(fr);
    ASSERT_FALSE(result.was_truncated);
    ASSERT_EQ(result.filtered_lines, 2);
}

TEST("applyHardLimit: over limit — truncates") {
    std::string big;
    for (int i = 0; i < MAX_OUTPUT_LINES + 50; ++i)
        big += "line " + std::to_string(i) + "\n";

    FilterResult fr;
    fr.output         = big;
    fr.original_lines = MAX_OUTPUT_LINES + 50;
    fr.filtered_lines = MAX_OUTPUT_LINES + 50;
    fr.was_truncated  = false;

    auto result = applyHardLimit(fr);
    ASSERT_TRUE(result.was_truncated);
    ASSERT_CONTAINS(result.output, "truncated at");
}

// ---- capRawBytes / splitLines byte-cap: nothing escapes Tkil ---------------
// Root cause (2026-07-07): every filter's short-circuit path (few-lines case
// in DefaultFilter et al) and applyHardLimit() itself only ever bounded LINE
// COUNT, never BYTE SIZE. A single pathological line (huge JSON/base64 blob,
// no newlines) sails through completely unfiltered -- confirmed in
// production telemetry: one `powershell -File ...` invocation emitted
// 472,581,003 raw bytes with filtered_bytes IDENTICAL (0% saved), because it
// had few enough "lines" to hit DefaultFilter's early return before
// applyHardLimit was ever called, AND applyHardLimit itself only checks
// line count. Fix: cap raw byte size INSIDE splitLines() -- the one function
// every one of the 19 registered filters calls first -- so no filter can
// possibly bypass it, regardless of line count.

TEST("capRawBytes: input under cap -> unchanged") {
    std::string small = "hello world";
    ASSERT_EQ(capRawBytes(small), small);
}

TEST("capRawBytes: input over cap -> output bounded") {
    std::string huge(MAX_RAW_BYTES * 3, 'x');
    auto capped = capRawBytes(huge);
    ASSERT_TRUE(capped.size() < huge.size());
    ASSERT_TRUE(capped.size() <= MAX_RAW_BYTES + 200);  // + marker overhead
}

TEST("capRawBytes: mentions omitted byte count in the marker") {
    std::string huge(MAX_RAW_BYTES * 2, 'y');
    auto capped = capRawBytes(huge);
    ASSERT_CONTAINS(capped, "bytes omitted");
}

TEST("capRawBytes: preserves head and tail content") {
    std::string huge = "HEAD_MARKER" + std::string(MAX_RAW_BYTES * 2, 'z') + "TAIL_MARKER";
    auto capped = capRawBytes(huge);
    ASSERT_CONTAINS(capped, "HEAD_MARKER");
    ASSERT_CONTAINS(capped, "TAIL_MARKER");
}

TEST("splitLines: a single giant line (no newlines) is bounded, not passed through raw") {
    // This is the exact production failure mode: one huge line, zero
    // newlines, so line-count-based limits never trigger.
    std::string one_giant_line(MAX_RAW_BYTES * 5, 'a');
    auto lines = splitLines(one_giant_line);
    size_t total = 0;
    for (auto& l : lines) total += l.size();
    ASSERT_TRUE(total < one_giant_line.size());
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
