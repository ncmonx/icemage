// Regression test: skill node content must NOT be truncated.
//
// User report (via another user): when icmg reads a skill file, it must show
// the WHOLE thing, not a 500-char summary. The indexer stores
// `context_nodes.content` for each skill; this verifies the content payload
// carries the full body.

#include "../test_main.hpp"
#include "../../src/cli/commands/skill_content.hpp"

#include <string>

using icmg::cli::buildSkillNodeContent;

TEST("skill content: full body is preserved (no 500-char cap)") {
    std::string desc = "Systematic debugging workflow";
    // Body deliberately well over the old 500-char cap.
    std::string body;
    for (int i = 0; i < 200; ++i) body += "line-" + std::to_string(i) + " content here\n";
    ASSERT_TRUE((int)body.size() > 500);

    std::string out = buildSkillNodeContent(desc, body);

    // Whole body must be present -- including the tail past 500 chars.
    ASSERT_TRUE(out.find("line-199 content here") != std::string::npos);
    ASSERT_TRUE((int)out.size() >= (int)body.size());
}

TEST("skill content: description leads the payload") {
    std::string out = buildSkillNodeContent("Lead desc", "body text");
    ASSERT_EQ(out.substr(0, 9), std::string("Lead desc"));
    ASSERT_TRUE(out.find("body text") != std::string::npos);
}

TEST("skill content: empty description falls back to body only") {
    ASSERT_EQ(buildSkillNodeContent("", "only body"), std::string("only body"));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
