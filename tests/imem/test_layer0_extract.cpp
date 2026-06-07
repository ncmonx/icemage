// 2026-06-07: Layer-0 rule-based auto-extract classifier (#luna-batch, STANDOUT).
#include "../test_main.hpp"
#include "../../src/imem/layer0_extract.hpp"

using namespace icmg::imem;

TEST("layer0: successful git commit -> wflog with subject") {
    auto r = classifyToolEvent("git commit -m x", "[main abc123] add feature\n 2 files changed", 0);
    ASSERT_EQ(r.kind, std::string("wflog"));
    ASSERT_CONTAINS(r.content, "commit:");
    ASSERT_CONTAINS(r.content, "add feature");
}

TEST("layer0: failed build error -> known-issue") {
    auto r = classifyToolEvent("cmake --build .", "foo.cpp:1: error: undefined reference to bar", 1);
    ASSERT_EQ(r.kind, std::string("known-issue"));
    ASSERT_CONTAINS(r.content, "error:");
}

TEST("layer0: LNK linker error -> known-issue") {
    auto r = classifyToolEvent("build.ps1", "LINK : fatal error LNK1104: cannot open file icmg.exe", 1);
    ASSERT_EQ(r.kind, std::string("known-issue"));
}

TEST("layer0: clean non-commit success -> skip") {
    ASSERT_EQ(classifyToolEvent("ls", "a\nb\nc", 0).kind, std::string("skip"));
}

TEST("layer0: nonzero exit WITHOUT error marker -> skip (no noise)") {
    ASSERT_EQ(classifyToolEvent("grep foo file", "", 1).kind, std::string("skip"));
}

TEST("layer0: firstMeaningfulLine skips blanks + trims CR") {
    ASSERT_EQ(firstMeaningfulLine("\n\n  hello\r\nworld"), std::string("hello"));
    ASSERT_EQ(firstMeaningfulLine(""), std::string(""));
}
