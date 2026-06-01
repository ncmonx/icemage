// v2.0.0 Phase 3 TDD: multimodal graph node builder. Ingested media (image/PDF
// OCR) becomes a first-class graph node (kind="multimodal") so context/graph/
// zone surface it. Verifies the PURE builder (no DB/filesystem):
//   - kind="multimodal", lang=mediaType, path preserved
//   - context = one-line preview of extracted text
//   - newlines/tabs collapse to single spaces
//   - long text truncated to 200 + "..."
//   - empty text → empty context, node still valid

#include "../test_main.hpp"
#include "../../src/graph/media_node.hpp"

#include <string>

using namespace icmg;

TEST("media node: basic fields") {
    auto n = graph::buildMediaNode("shots/err.png", "image", "Segfault at line 42");
    ASSERT_EQ(n.kind, std::string("multimodal"));
    ASSERT_EQ(n.lang, std::string("image"));
    ASSERT_EQ(n.path, std::string("shots/err.png"));
    ASSERT_EQ(n.context, std::string("Segfault at line 42"));
}

TEST("media node: newlines collapse to single line") {
    auto n = graph::buildMediaNode("doc.pdf", "pdf", "line one\n\nline two\tcol");
    ASSERT_EQ(n.context, std::string("line one line two col"));
}

TEST("media node: leading/trailing whitespace trimmed") {
    auto n = graph::buildMediaNode("a.png", "image", "   hello   ");
    ASSERT_EQ(n.context, std::string("hello"));
}

TEST("media node: long text truncated with marker") {
    std::string big(500, 'x');
    auto n = graph::buildMediaNode("big.png", "image", big);
    // 200 chars + "..." = 203
    ASSERT_EQ((int)n.context.size(), 203);
    ASSERT_CONTAINS(n.context, "...");
}

TEST("media node: empty text → empty context, kind kept") {
    auto n = graph::buildMediaNode("blank.png", "image", "");
    ASSERT_EQ((int)n.context.size(), 0);
    ASSERT_EQ(n.kind, std::string("multimodal"));
    ASSERT_EQ((int)n.size_bytes, 0);
}

TEST("media node: preview helper standalone") {
    ASSERT_EQ(graph::mediaPreviewLine("a\nb"), std::string("a b"));
    ASSERT_EQ(graph::mediaPreviewLine(""), std::string(""));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
