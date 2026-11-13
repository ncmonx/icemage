// TDD (2026-11-12): compaction fidelity (research item 7, after CliffCompaction
// arXiv 2609.26779: compaction may only truncate/drop, never rephrase, and must
// never compact a compaction). Live evidence: two auto-compact-* LLM nodes on
// 2026-05-31 restate the same facts 2 minutes apart, one carrying a "Postgres /
// migration 0021" fact found in no memory row.
#include "../test_main.hpp"
#include "../../src/cli/compaction_digest.hpp"
#include "../../src/core/utf8_prefix.hpp"

using namespace icmg::cli;
using icmg::imem::MemoryNode;

static MemoryNode node(int64_t id, const std::string& topic, const std::string& content) {
    MemoryNode n; n.id = id; n.topic = topic; n.content = content; return n;
}

TEST("utf8Prefix: never splits a multi-byte character") {
    const std::string s = "ab\xF0\x9F\xA5\xB0z";   // "ab" + U+1F970 (4 bytes) + "z"
    ASSERT_EQ(icmg::core::utf8Prefix(s, 3), std::string("ab"));
    ASSERT_EQ(icmg::core::utf8Prefix(s, 5), std::string("ab"));
    ASSERT_EQ(icmg::core::utf8Prefix(s, 6), std::string("ab\xF0\x9F\xA5\xB0"));
}

TEST("utf8Prefix: fits -> unchanged") {
    ASSERT_EQ(icmg::core::utf8Prefix("hello", 5), std::string("hello"));
    ASSERT_EQ(icmg::core::utf8Prefix("hello", 99), std::string("hello"));
    ASSERT_EQ(icmg::core::utf8Prefix("hello", 0), std::string(""));
}

TEST("sentenceEnd: version dots and decimals do not end a sentence") {
    ASSERT_EQ(sentenceEnd("v2.28.0 shipped. Next"), (size_t)16);
    ASSERT_EQ(sentenceEnd("speed 1.5x faster"), std::string::npos);
    ASSERT_EQ(sentenceEnd("done!"), (size_t)5);
    ASSERT_EQ(sentenceEnd("line one\nline two."), (size_t)8);
}

TEST("compactionInputs: drops compaction products (never compact a compaction)") {
    std::vector<MemoryNode> hits = {
        node(1, "auto-compact-20260531-123316", "- old summary"),
        node(2, "decisions-release", "v2.28.0 shipped."),
        node(3, "session:kemarin", "Q: earlier summary"),
        node(4, "session-snapshot auto-precompact-1", "Session: x"),
        node(5, "errors-resolved", "fix: utf8 cut."),
    };
    auto in = compactionInputs(hits, 10);
    ASSERT_EQ(in.size(), (size_t)2);
    ASSERT_EQ(in[0].id, (int64_t)2);
    ASSERT_EQ(in[1].id, (int64_t)5);
}

TEST("compactionInputs: caps to limit, keeps recall order") {
    std::vector<MemoryNode> hits;
    for (int i = 1; i <= 6; ++i) hits.push_back(node(i, "decisions-x", "fact."));
    auto in = compactionInputs(hits, 3);
    ASSERT_EQ(in.size(), (size_t)3);
    ASSERT_EQ(in[2].id, (int64_t)3);
}

TEST("extractiveDigest: verbatim first sentence + source id per line") {
    std::vector<MemoryNode> hits = {
        node(7, "decisions-release", "v2.28.0 shipped publik. Second sentence dropped."),
        node(9, "errors-resolved", "fix utf8 cut\nmore lines"),
    };
    std::string d = extractiveDigest(hits, 8, 140);
    ASSERT_CONTAINS(d, "- decisions-release: v2.28.0 shipped publik [#7]\n");
    ASSERT_CONTAINS(d, "- errors-resolved: fix utf8 cut [#9]\n");
    ASSERT_TRUE(d.find("Second sentence") == std::string::npos);
}

TEST("extractiveDigest: long sentence cut on a UTF-8 boundary with an ellipsis") {
    std::string longs(130, 'x');
    longs += "\xF0\x9F\xA5\xB0\xF0\x9F\xA5\xB0\xF0\x9F\xA5\xB0 tail";
    std::vector<MemoryNode> hits = {node(3, "t", longs)};
    std::string d = extractiveDigest(hits, 8, 140);
    // 137-byte budget before "...": 130 x + one 4-byte emoji fits (134), the
    // next one would end at 138 > 137 -> cut there, never mid-character.
    std::string want = std::string(130, 'x') + "\xF0\x9F\xA5\xB0...";
    ASSERT_CONTAINS(d, "- t: " + want + " [#3]\n");
}

TEST("extractiveDigest: at most maxLines lines") {
    std::vector<MemoryNode> hits;
    for (int i = 1; i <= 12; ++i) hits.push_back(node(i, "d", "f."));
    std::string d = extractiveDigest(hits, 8, 140);
    size_t lines = 0;
    for (char c : d) if (c == '\n') ++lines;
    ASSERT_EQ(lines, (size_t)8);
}

TEST("sourceIdsLine: lists every input id for provenance") {
    std::vector<MemoryNode> hits = {node(4, "a", "x"), node(11, "b", "y")};
    ASSERT_EQ(sourceIdsLine(hits), std::string("src: #4 #11\n"));
    ASSERT_EQ(sourceIdsLine({}), std::string(""));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
