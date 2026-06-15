// TDD (2026-06-15): <private> redaction for `icmg store`.
// Spec backlog: docs/2026-06-15-recall-progressive-disclosure.md sec 11.
// Riset asal: claude-mem "<private> tags to exclude sensitive content from storage".
// Pure helper so redaction is unit-testable without touching the DB.
// Failing FIRST: src/cli/private_filter.hpp does not exist yet.

#include "../test_main.hpp"
#include "../../src/cli/private_filter.hpp"

#include <string>

using icmg::cli::stripPrivate;
using icmg::cli::hasPrivate;

// 1. A single <private>...</private> span is removed; surrounding text stays.
TEST("private-filter: strips a single private span, keeps the rest") {
    std::string in  = "api key is <private>sk-secret-123</private> ok";
    std::string out = stripPrivate(in);
    ASSERT_NOT_CONTAINS(out, "sk-secret-123");
    ASSERT_NOT_CONTAINS(out, "<private>");
    ASSERT_NOT_CONTAINS(out, "</private>");
    ASSERT_CONTAINS(out, "api key is");
    ASSERT_CONTAINS(out, "ok");
}

// 2. Multiple spans all removed.
TEST("private-filter: strips multiple private spans") {
    std::string in  = "a <private>X</private> b <private>Y</private> c";
    std::string out = stripPrivate(in);
    ASSERT_NOT_CONTAINS(out, "X");
    ASSERT_NOT_CONTAINS(out, "Y");
    ASSERT_CONTAINS(out, "a");
    ASSERT_CONTAINS(out, "b");
    ASSERT_CONTAINS(out, "c");
}

// 3. Multiline span removed (content with newlines inside the tags).
TEST("private-filter: strips a multiline private span") {
    std::string in  = "before <private>line1\nline2\nsecret</private> after";
    std::string out = stripPrivate(in);
    ASSERT_NOT_CONTAINS(out, "secret");
    ASSERT_NOT_CONTAINS(out, "line1");
    ASSERT_CONTAINS(out, "before");
    ASSERT_CONTAINS(out, "after");
}

// 4. No tags -> content unchanged.
TEST("private-filter: content without tags is returned unchanged") {
    std::string in = "just a normal note, nothing secret";
    ASSERT_EQ(stripPrivate(in), in);
}

// 5. hasPrivate detects presence of a tag pair.
TEST("private-filter: hasPrivate true only when a span exists") {
    ASSERT_TRUE(hasPrivate("x <private>y</private> z"));
    ASSERT_FALSE(hasPrivate("no tags here"));
}

// 6. Unterminated <private> (no closing tag) -> redact to end (fail-safe:
//    never leak what was marked private even if malformed).
TEST("private-filter: unterminated open tag redacts to end of string") {
    std::string in  = "keep this <private>secret tail with no close";
    std::string out = stripPrivate(in);
    ASSERT_NOT_CONTAINS(out, "secret tail");
    ASSERT_CONTAINS(out, "keep this");
}
