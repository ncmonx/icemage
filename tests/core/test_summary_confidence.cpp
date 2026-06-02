// v2.0.0 TE1 (token-efficiency v2): confidence-gated summarization. A local-model
// or heuristic summary that over-trims or drops key identifiers must NOT be shipped
// to the main LLM (hallucination risk). summaryConfidence scores a summary in [0,1]
// from identifier-retention x length-sanity; acceptSummary gates on a threshold.

#include "../test_main.hpp"
#include "../../src/core/summary_confidence.hpp"

#include <string>

using namespace icmg::core;

namespace {
const char* ORIG =
    "void GraphStore::resolveAndInsertEdges(const Scanner& scanner) {\n"
    "    for (auto& node : scanner.nodes()) {\n"
    "        insertEdge(node.id, resolveTarget(node.import_path));\n"
    "    }\n"
    "    buildXRefEdges();\n"
    "}";
}

TEST("summaryConfidence: good summary keeps identifiers + sane length -> high") {
    std::string s = "GraphStore::resolveAndInsertEdges iterates scanner nodes, "
                    "calls insertEdge with resolveTarget(import_path), then buildXRefEdges.";
    double c = summaryConfidence(ORIG, s);
    ASSERT_TRUE(c >= 0.5);
    ASSERT_TRUE(acceptSummary(ORIG, s, 0.5));
}

TEST("summaryConfidence: over-trimmed summary -> low, rejected") {
    std::string s = "graph fn.";          // drops every key identifier
    double c = summaryConfidence(ORIG, s);
    ASSERT_TRUE(c < 0.5);
    ASSERT_FALSE(acceptSummary(ORIG, s, 0.5));
}

TEST("summaryConfidence: no-compression (summary ~= original) -> low len-sanity") {
    double c = summaryConfidence(ORIG, std::string(ORIG));
    ASSERT_TRUE(c < 0.5);   // not a useful summary — barely shorter
}

TEST("summaryConfidence: identifier-drop despite ok length -> low") {
    // Reasonable length but pure prose, no source identifiers retained.
    std::string s = "This function loops over some things and does work on each one "
                    "and finally performs a secondary pass over the collection.";
    double c = summaryConfidence(ORIG, s);
    ASSERT_TRUE(c < 0.5);
}

TEST("summaryConfidence: empty summary -> 0") {
    ASSERT_EQ((int)(summaryConfidence(ORIG, "") * 100), 0);
    ASSERT_FALSE(acceptSummary(ORIG, "", 0.5));
}

TEST("summaryConfidence: empty original -> 0 (nothing to trust)") {
    ASSERT_EQ((int)(summaryConfidence("", "anything") * 100), 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
