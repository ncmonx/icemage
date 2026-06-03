// Command recommender core: rank known commands against a natural-language intent
// so the long tail (rarely-remembered commands) becomes discoverable. Pure +
// model-free (reuses promptJaccard); the CLI layer feeds it the live registry.
#include "../test_main.hpp"
#include "../../src/core/command_suggest.hpp"
#include <string>
using namespace icmg::core;

static std::vector<CmdDoc> sampleDocs() {
    return {
        {"context",        "Read a large file as graph + symbols + memory bundle"},
        {"reverse-impact", "Trace which symbols depend on a function (impact analysis)"},
        {"recall",         "Recall a past decision from memory"},
        {"compress",       "Compress large command output into a glossary"},
        {"fetch",          "Fetch a URL with cache and token reduction"},
    };
}

TEST("rankCommands: intent maps to the most relevant command") {
    auto hits = rankCommands("trace which functions depend on this symbol", sampleDocs(), 3);
    ASSERT_TRUE(!hits.empty());
    ASSERT_EQ(hits[0].name, std::string("reverse-impact"));
}

TEST("rankCommands: results are sorted by score descending") {
    auto hits = rankCommands("recall a past decision from memory", sampleDocs(), 5);
    ASSERT_TRUE(hits.size() >= 2);
    for (size_t i = 1; i < hits.size(); ++i)
        ASSERT_TRUE(hits[i - 1].score >= hits[i].score);
}

TEST("rankCommands: topN caps the result count") {
    auto hits = rankCommands("read a file", sampleDocs(), 2);
    ASSERT_TRUE(hits.size() <= 2);
}

TEST("rankCommands: zero-overlap intent yields no hits") {
    auto hits = rankCommands("xyzzy quux frobnicate", sampleDocs(), 5);
    ASSERT_TRUE(hits.empty());
}

TEST("rankCommands: empty docs yields no hits") {
    std::vector<CmdDoc> none;
    auto hits = rankCommands("anything at all", none, 5);
    ASSERT_TRUE(hits.empty());
}
