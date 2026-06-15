// TDD (2026-06-15): progressive-disclosure recall index helpers.
// Spec: docs/2026-06-15-recall-progressive-disclosure.md
// Pure-function layer (mirrors recall_json.hpp pattern) so the index format,
// typed-icon mapping, and semantic-title derivation are unit-testable without
// running the binary. Failing FIRST: src/cli/recall_index.hpp does not exist yet.

#include "../test_main.hpp"
#include "../../src/cli/recall_index.hpp"
#include "../../src/imem/memory_node.hpp"

#include <string>
#include <vector>

using icmg::imem::MemoryNode;
using icmg::cli::makeTitle;
using icmg::cli::iconFor;
using icmg::cli::formatIndexLine;
using icmg::cli::formatIndex;

static MemoryNode mk(int64_t id, const std::string& topic,
                     const std::string& content, int importance = 1) {
    MemoryNode n;
    n.id = id; n.topic = topic; n.content = content; n.importance = importance;
    n.frequency = 1; n.score = 1.0;
    return n;
}

// 1. Title is compact: <= 10 words, single line, no trailing newline.
TEST("recall-index: makeTitle truncates long content to <=10 words, no newline") {
    std::string content =
        "this is a very long memory content with many many words\n"
        "second line should never appear in the title at all";
    std::string title = makeTitle(content);
    ASSERT_NOT_CONTAINS(title, "\n");
    ASSERT_NOT_CONTAINS(title, "second line");
    // word count <= 10
    int words = 0; bool inw = false;
    for (char c : title) { if (c == ' ') inw = false; else if (!inw) { inw = true; ++words; } }
    ASSERT_TRUE(words <= 10);
}

// 2. Typed-icon mapping (Option A, zero-migration: topic prefix + importance).
TEST("recall-index: iconFor maps decisions topic to decision icon") {
    ASSERT_CONTAINS(std::string(iconFor(mk(1, "decisions-recall", "x"))), "\xF0\x9F\x9F\xA4"); // 🟤
}
TEST("recall-index: iconFor maps critical importance to gotcha icon") {
    ASSERT_CONTAINS(std::string(iconFor(mk(1, "notes", "x", 3))), "\xF0\x9F\x94\xB4"); // 🔴
}
TEST("recall-index: iconFor maps fix/bug topic to problem-solution icon") {
    ASSERT_CONTAINS(std::string(iconFor(mk(1, "fix-parser-bug", "x"))), "\xF0\x9F\x9F\xA1"); // 🟡
}
TEST("recall-index: iconFor maps research topic to discovery icon") {
    ASSERT_CONTAINS(std::string(iconFor(mk(1, "decisions-research", "x", 1))), "\xF0\x9F\x9F\xA3"); // 🟣
    // note: decisions- prefix wins only if not research; research check first
}
TEST("recall-index: iconFor default is how-it-works icon") {
    ASSERT_CONTAINS(std::string(iconFor(mk(1, "misc", "x"))), "\xF0\x9F\x94\xB5"); // 🔵
}

// 3. Index line carries the ID and a ~token estimate, NOT the full content.
TEST("recall-index: formatIndexLine has #id and ~token, hides full long content") {
    std::string longc(400, 'a');  // 400 bytes ~ 100 tok
    MemoryNode n = mk(1842, "decisions-context-budget", longc);
    std::string line = formatIndexLine(n);
    ASSERT_CONTAINS(line, "#1842");
    ASSERT_CONTAINS(line, "~");
    ASSERT_NOT_CONTAINS(line, longc);   // full content must not leak into index
}

// 4. Token estimate is monotonic: longer content -> larger ~N.
TEST("recall-index: token estimate grows with content length") {
    auto tokOf = [](const std::string& line) -> int {
        auto p = line.find('~');
        if (p == std::string::npos) return -1;
        int v = 0; ++p;
        while (p < line.size() && line[p] >= '0' && line[p] <= '9') { v = v*10 + (line[p]-'0'); ++p; }
        return v;
    };
    int small = tokOf(formatIndexLine(mk(1, "t", std::string(40, 'a'))));
    int big   = tokOf(formatIndexLine(mk(2, "t", std::string(800, 'a'))));
    ASSERT_TRUE(small > 0);
    ASSERT_TRUE(big > small);
}

// 5. formatIndex groups by topic: each topic header appears once, all IDs listed.
TEST("recall-index: formatIndex groups by topic with single header per topic") {
    std::vector<MemoryNode> nodes = {
        mk(10, "decisions-context-budget", "alpha note one"),
        mk(11, "decisions-context-budget", "beta note two"),
        mk(12, "decisions-research", "claude-mem progressive disclosure"),
    };
    std::string out = formatIndex(nodes, "topic");
    ASSERT_CONTAINS(out, "#10");
    ASSERT_CONTAINS(out, "#11");
    ASSERT_CONTAINS(out, "#12");
    // topic header text present
    ASSERT_CONTAINS(out, "decisions-context-budget");
    ASSERT_CONTAINS(out, "decisions-research");
    // single header per topic: "decisions-context-budget" appears once as a header line
    size_t first = out.find("decisions-context-budget");
    size_t second = out.find("decisions-context-budget", first + 1);
    ASSERT_TRUE(second == std::string::npos);  // exactly one occurrence (header only, not per-row)
}

// 6. Empty input -> empty (or no-result) string, no crash.
TEST("recall-index: formatIndex on empty list does not crash") {
    std::string out = formatIndex({}, "topic");
    ASSERT_NOT_CONTAINS(out, "#");
}
