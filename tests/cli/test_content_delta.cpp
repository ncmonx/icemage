// TDD (2026-06-15): `icmg context <file> --diff` delta re-read.
// Spec: when a file was already emitted this session and is re-requested after
// an edit, emit ONLY the changed lines (with line numbers, collapsing unchanged
// runs) instead of the whole body — a token saver for iterative re-reads.
// Pure helper so the delta logic is unit-testable without a DB or filesystem.
// Failing FIRST: src/cli/content_delta.hpp does not exist yet.

#include "../test_main.hpp"
#include "../../src/cli/content_delta.hpp"

#include <string>

using icmg::cli::computeContentDelta;

// 1. Identical content => identical flag set, zero changed lines, empty render.
TEST("content-delta: identical content yields no changes") {
    std::string body = "line one\nline two\nline three\n";
    auto r = computeContentDelta(body, body, 2);
    ASSERT_TRUE(r.identical);
    ASSERT_EQ(r.changed_lines, 0);
    ASSERT_TRUE(r.text.empty());
}

// 2. A single changed line in the middle of a big file: only the change (plus a
//    little context) is rendered; far-away unique unchanged lines are collapsed.
TEST("content-delta: single middle change collapses unchanged runs") {
    std::string prev =
        "alpha\nbravo\ncharlie\ndelta\necho\nfoxtrot\ngolf\nhotel\nindia\njuliet\n";
    std::string cur =
        "alpha\nbravo\ncharlie\ndelta\nECHO_CHANGED\nfoxtrot\ngolf\nhotel\nindia\njuliet\n";
    auto r = computeContentDelta(prev, cur, 2);
    ASSERT_FALSE(r.identical);
    ASSERT_TRUE(r.changed_lines >= 1);
    ASSERT_CONTAINS(r.text, "ECHO_CHANGED");
    // A far unique unchanged line should be collapsed away, not re-emitted.
    ASSERT_NOT_CONTAINS(r.text, "alpha");
    ASSERT_NOT_CONTAINS(r.text, "juliet");
    // The collapse marker should be present.
    ASSERT_CONTAINS(r.text, "unchanged");
}

// 3. Appended lines show up as changed.
TEST("content-delta: appended lines are reported as changed") {
    std::string prev = "one\ntwo\nthree\n";
    std::string cur  = "one\ntwo\nthree\nFOUR_NEW\nFIVE_NEW\n";
    auto r = computeContentDelta(prev, cur, 1);
    ASSERT_FALSE(r.identical);
    ASSERT_CONTAINS(r.text, "FOUR_NEW");
    ASSERT_CONTAINS(r.text, "FIVE_NEW");
}

// 4. The delta is strictly smaller than the full current body when most of a
//    large file is unchanged (the whole point of the feature).
TEST("content-delta: delta smaller than full body on mostly-unchanged file") {
    std::string prev, cur;
    for (int i = 0; i < 200; ++i) {
        std::string ln = "stable_line_" + std::to_string(i) + "\n";
        prev += ln;
        cur  += (i == 100) ? "the_one_edit\n" : ln;
    }
    auto r = computeContentDelta(prev, cur, 2);
    ASSERT_FALSE(r.identical);
    ASSERT_TRUE(r.text.size() < cur.size());
    ASSERT_CONTAINS(r.text, "the_one_edit");
}

// 5. Changed line keeps its (1-based, cur-side) line number so edits can anchor.
TEST("content-delta: preserves cur-side line number of the change") {
    std::string prev = "a\nb\nc\nd\ne\n";
    std::string cur  = "a\nb\nCHANGED\nd\ne\n";   // line 3 changed
    auto r = computeContentDelta(prev, cur, 0);
    ASSERT_CONTAINS(r.text, "CHANGED");
    ASSERT_CONTAINS(r.text, "3");   // line number 3 rendered next to it
}

// ---- Feature C (2026-06-15): auto-default diff decision -----------------
using icmg::cli::shouldContextDiff;

// 6. First call (no baseline, no flags) => full body, not a diff.
TEST("context-autodiff: no baseline yields full body") {
    ASSERT_TRUE(!shouldContextDiff(/*explicit*/false, /*no_diff*/false,
                                   /*baseline*/false, /*reset*/false));
}

// 7. Re-read with an existing baseline => auto-diff (no flag needed).
TEST("context-autodiff: baseline present auto-diffs") {
    ASSERT_TRUE(shouldContextDiff(false, false, /*baseline*/true, false));
}

// 8. --no-diff (or --full) opts out even when a baseline exists.
TEST("context-autodiff: no_diff opts out") {
    ASSERT_TRUE(!shouldContextDiff(false, /*no_diff*/true, /*baseline*/true, false));
}

// 9. Explicit --diff forces the diff path even with no baseline (seed call).
TEST("context-autodiff: explicit diff forces path") {
    ASSERT_TRUE(shouldContextDiff(/*explicit*/true, false, /*baseline*/false, false));
}

// 10. --diff-reset always shows full (clears + reseeds), even with a baseline.
TEST("context-autodiff: reset shows full") {
    ASSERT_TRUE(!shouldContextDiff(false, false, /*baseline*/true, /*reset*/true));
}
