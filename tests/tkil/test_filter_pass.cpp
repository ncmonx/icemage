// Phase 68 T1: unit tests for Phase 67 T27 filter passes.
#include "../test_main.hpp"
#include "../../src/tkil/filters/filter_utils.hpp"

using icmg::tkil::stripAnsi;
using icmg::tkil::dedupConsecutive;
using icmg::tkil::collapseBlankRuns;
using icmg::tkil::splitLines;

TEST("stripAnsi: removes CSI color codes") {
    auto r = stripAnsi("\x1b[31mRED\x1b[0m end");
    ASSERT_EQ(r, std::string("RED end"));
}

TEST("stripAnsi: no-op when no escapes") {
    auto r = stripAnsi("plain text");
    ASSERT_EQ(r, std::string("plain text"));
}

TEST("stripAnsi: collapses CR-overwrite") {
    auto r = stripAnsi("foo\rbar");
    ASSERT_EQ(r, std::string("bar"));
}

TEST("stripAnsi: preserves CRLF") {
    auto r = stripAnsi("line1\r\nline2");
    ASSERT_EQ(r, std::string("line1\r\nline2"));
}

TEST("dedupConsecutive: 4 dups → 1 line ×4") {
    std::vector<std::string> in = {"a", "a", "a", "a", "b"};
    auto r = dedupConsecutive(in);
    ASSERT_EQ((int)r.size(), 2);
    ASSERT_EQ(r[0], std::string("a  ×4"));
    ASSERT_EQ(r[1], std::string("b"));
}

TEST("dedupConsecutive: no dups passes through") {
    std::vector<std::string> in = {"a", "b", "c"};
    auto r = dedupConsecutive(in);
    ASSERT_EQ((int)r.size(), 3);
    ASSERT_EQ(r[0], std::string("a"));
}

TEST("dedupConsecutive: empty lines not deduped") {
    std::vector<std::string> in = {"", "", ""};
    auto r = dedupConsecutive(in);
    ASSERT_EQ((int)r.size(), 3);
}

TEST("collapseBlankRuns: 4 blanks → 1 blank") {
    std::vector<std::string> in = {"a", "", "", "", "", "b"};
    auto r = collapseBlankRuns(in);
    ASSERT_EQ((int)r.size(), 3);
    ASSERT_EQ(r[1], std::string(""));
}

TEST("splitLines: ANSI stripped + lines split") {
    auto r = splitLines("\x1b[1mone\x1b[0m\ntwo");
    ASSERT_EQ((int)r.size(), 2);
    ASSERT_EQ(r[0], std::string("one"));
    ASSERT_EQ(r[1], std::string("two"));
}

int main() {
    std::cout << "=== filter pass tests ===\n";
    return icmg::test::run_all();
}
