// P3 fix (2026-06-14): `icmg context` misreported a 0-byte-but-openable file
// as "Content unavailable (graph path mismatch)". Root cause: success gated on
// `!body.empty()` instead of "did any candidate path open?". classifyContent
// makes that 3-way decision pure + testable.
//
// RED evidence (empirical, pre-fix): `icmg context <fresh-0-byte-file>` printed
// "Content unavailable (graph path mismatch)" though the path was correct and
// the file had just been scanned into the graph.
#include "../test_main.hpp"
#include "../../src/cli/content_status.hpp"

using namespace icmg::cli;

// ---- Test 1: opened + has content -> Ok -------------------------------------
TEST("content_status: resolved path with body -> Ok") {
    ASSERT_TRUE(classifyContent("D:/proj/file.cpp", "int main(){}") == ContentStatus::Ok);
}

// ---- Test 2: opened but empty (0 bytes) -> Empty, NOT Unavailable -----------
TEST("content_status: resolved path but empty body -> Empty (not a path mismatch)") {
    // This is the bug: a valid, openable 0-byte file (now.md after NDC rotation,
    // a fresh empty file) must classify as Empty — never Unavailable.
    ASSERT_TRUE(classifyContent("D:/proj/now.md", "") == ContentStatus::Empty);
    ASSERT_TRUE(classifyContent("D:/proj/now.md", "") != ContentStatus::Unavailable);
}

// ---- Test 3: no candidate opened -> Unavailable -----------------------------
TEST("content_status: empty resolved -> Unavailable (true path mismatch)") {
    ASSERT_TRUE(classifyContent("", "") == ContentStatus::Unavailable);
    ASSERT_TRUE(classifyContent("", "ignored") == ContentStatus::Unavailable);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
