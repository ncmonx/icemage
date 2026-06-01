// v1.56 T1 Stage 2: dedup pass — adjacent identical/near-identical lines
// collapse to "<line> (×N)". Always-verbatim for error patterns.

#include "../test_main.hpp"
#include "../../src/tkil/dedup_pass.hpp"

using namespace icmg::tkil;

TEST("dedupPass: empty input returns empty") {
    ASSERT_EQ(dedupPass("", {}), std::string(""));
}

TEST("dedupPass: single line unchanged") {
    ASSERT_EQ(dedupPass("hello\n", {}), std::string("hello\n"));
}

TEST("dedupPass: all distinct lines pass through unchanged") {
    std::string in  = "alpha\nbeta\ngamma\n";
    ASSERT_EQ(dedupPass(in, {}), in);
}

TEST("dedupPass: 3 identical adjacent lines collapse with count") {
    std::string in  = "added pkg\nadded pkg\nadded pkg\n";
    std::string out = dedupPass(in, {});
    // Expect single line annotated with (×3).
    ASSERT_TRUE(out.find("added pkg") != std::string::npos);
    ASSERT_TRUE(out.find("(\xc3\x97""3)") != std::string::npos
             || out.find("(x3)") != std::string::npos);
    // Output must be shorter than input.
    ASSERT_TRUE(out.size() < in.size());
}

TEST("dedupPass: 2 identical adjacent lines do NOT collapse (need >=3 by default)") {
    // Conservative: small repetition (=2) should not collapse to avoid hiding
    // legitimate adjacent duplicates from real output.
    std::string in  = "added pkg\nadded pkg\n";
    std::string out = dedupPass(in, {});
    ASSERT_EQ(out, in);
}

TEST("dedupPass: error pattern never collapsed (allowlist)") {
    std::string in  = "error: LNK2001 missing symbol foo\n"
                       "error: LNK2001 missing symbol foo\n"
                       "error: LNK2001 missing symbol foo\n";
    std::string out = dedupPass(in, {});
    // All 3 must remain verbatim — count any non-overlapping occurrences.
    size_t count = 0, pos = 0;
    while ((pos = out.find("LNK2001", pos)) != std::string::npos) {
        ++count; ++pos;
    }
    ASSERT_EQ(count, 3u);
}

TEST("dedupPass: distinct line between duplicates breaks the run") {
    std::string in  = "x\nx\nx\ny\nx\nx\nx\n";
    std::string out = dedupPass(in, {});
    // Two collapsed runs of x, one y line in between.
    size_t x_count = 0, pos = 0;
    while ((pos = out.find("x", pos)) != std::string::npos) {
        ++x_count; ++pos;
    }
    // Each collapsed "x (×3)" still contains one 'x' literal.
    ASSERT_EQ(x_count, 2u);
    ASSERT_TRUE(out.find("y") != std::string::npos);
}

TEST("dedupPass: near-identical via prefix ratio collapses") {
    DedupOpts opts;
    opts.prefix_ratio = 0.8;
    // 3 lines share ≥80% prefix → near-dup.
    // Use long shared prefix so ratio comfortably > 0.8 (47/50 = 0.94).
    std::string in  = "[building] D:/very/long/project/path/src/aaa.cpp\n"
                       "[building] D:/very/long/project/path/src/bbb.cpp\n"
                       "[building] D:/very/long/project/path/src/ccc.cpp\n";
    std::string out = dedupPass(in, opts);
    // Expect collapsed marker present, output shorter than input.
    ASSERT_TRUE(out.size() < in.size());
    ASSERT_TRUE(out.find("(\xc3\x97""3)") != std::string::npos
             || out.find("(x3)") != std::string::npos);
}

TEST("dedupPass: prefix-ratio disabled (1.0) keeps near-identical lines distinct") {
    DedupOpts opts;
    opts.prefix_ratio = 1.0;   // require exact match
    std::string in  = "[building] aaa.cpp\n[building] bbb.cpp\n[building] ccc.cpp\n";
    std::string out = dedupPass(in, opts);
    ASSERT_EQ(out, in);
}

TEST("dedupPass: trailing newline preserved") {
    std::string in  = "line\nline\nline\n";
    std::string out = dedupPass(in, {});
    ASSERT_TRUE(out.back() == '\n');
}

TEST("dedupPass: no trailing newline tolerated") {
    std::string in  = "line\nline\nline";   // no \n at end
    std::string out = dedupPass(in, {});
    // Should still collapse; final newline behaviour: match input (no trailing \n).
    ASSERT_TRUE(out.find("line") != std::string::npos);
}

TEST("dedupPass: FATAL allowlist") {
    std::string in  = "FATAL: heap corruption\nFATAL: heap corruption\nFATAL: heap corruption\n";
    std::string out = dedupPass(in, {});
    ASSERT_EQ(out, in);   // never collapse
}

TEST("dedupPass: C-error code MSVC allowlist (C2065)") {
    std::string in  = "C2065: undeclared identifier 'foo'\n"
                       "C2065: undeclared identifier 'foo'\n"
                       "C2065: undeclared identifier 'foo'\n";
    std::string out = dedupPass(in, {});
    size_t count = 0, pos = 0;
    while ((pos = out.find("C2065", pos)) != std::string::npos) {
        ++count; ++pos;
    }
    ASSERT_EQ(count, 3u);
}
