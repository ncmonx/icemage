// M8 T4: microcompaction — surgical tail-preserve truncation for large outputs.
// Technique from claude-code microCompact.ts: keep last N chars when over threshold.
#include "../test_main.hpp"
#include <string>
#include <cstddef>

// Microcompact: if text > threshold, keep last keep_bytes bytes + prepend notice.
// Never modifies text shorter than threshold.
inline std::string microcompact(const std::string& text,
                                 std::size_t threshold = 40 * 1024,
                                 std::size_t keep_bytes = 40 * 1024) {
    if (text.size() <= threshold) return text;
    std::size_t tail_start = text.size() > keep_bytes ? text.size() - keep_bytes : 0;
    return std::string("[microcompact: ") + std::to_string(text.size()) +
           " bytes -> kept last " + std::to_string(text.size() - tail_start) +
           " bytes]\n" + text.substr(tail_start);
}

TEST("microcompact: small text unchanged") {
    std::string s(1000, 'x');
    ASSERT_EQ(microcompact(s, 40*1024), s);
}

TEST("microcompact: large text truncated to keep_bytes tail") {
    std::string s(50000, 'a');
    s[49000] = 'Z'; // marker near end
    auto result = microcompact(s, 40*1024, 40*1024);
    ASSERT_TRUE(result.size() < s.size());
    ASSERT_CONTAINS(result, "microcompact:");
    ASSERT_CONTAINS(result, "Z"); // marker preserved (in tail)
}

TEST("microcompact: prefix not in kept tail is dropped") {
    std::string s(50000, 'a');
    s[100] = 'X'; // marker near start — should be dropped
    auto result = microcompact(s, 40*1024, 40*1024);
    // result has notice + 40K tail; X at pos 100 is gone
    auto notice_end = result.find('\n');
    std::string tail = result.substr(notice_end + 1);
    ASSERT_TRUE(tail.find('X') == std::string::npos);
}

TEST("microcompact: exact threshold boundary not truncated") {
    std::string s(40 * 1024, 'b');
    ASSERT_EQ(microcompact(s, 40*1024), s);
}
