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

TEST("rankCommands: a command named in the intent beats a desc-only match") {
    std::vector<CmdDoc> docs = {
        {"expand",   "Expand compressed output back to full text"},  // desc has 'compressed/output'
        {"compress", "Shrink large output into a glossary"},
    };
    auto hits = rankCommands("compress large output", docs, 2);
    ASSERT_TRUE(!hits.empty());
    ASSERT_EQ(hits[0].name, std::string("compress"));   // name match wins over desc overlap
}

TEST("rankCommands: nameRecall is 1.0 when the full name appears in the intent") {
    ASSERT_TRUE(nameRecall("please fetch a url now", "fetch") > 0.999);
}

TEST("rankCommands: nameRecall is 0 when the name is absent") {
    ASSERT_TRUE(nameRecall("trace the dependencies", "fetch") < 0.001);
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

// Synonym/keywords routing: "who calls X" must reach the CALLERS command even
// though its description says "callers" (token != "calls"). The keywords field
// carries the synonyms so the intent matches without polluting the displayed
// description. This is the router-precision fix.
TEST("rankCommands: keywords route synonym intent to the right command") {
    std::vector<CmdDoc> docs = {
        {"graph-callees", "Show what a symbol calls", "callees outbound invokes what-it-calls"},
        {"graph-callers", "Show callers of a symbol", "who calls invoked-by inbound used-by-function"},
    };
    auto hits = rankCommands("who calls this function", docs, 2);
    ASSERT_TRUE(!hits.empty());
    ASSERT_EQ(hits[0].name, std::string("graph-callers"));
}

TEST("rankCommands: keywords are optional (back-compat 2-field docs)") {
    std::vector<CmdDoc> docs = {
        {"compress", "Shrink large output into a glossary"},  // no keywords field
    };
    auto hits = rankCommands("compress large output", docs, 2);
    ASSERT_TRUE(!hits.empty());
    ASSERT_EQ(hits[0].name, std::string("compress"));
}
