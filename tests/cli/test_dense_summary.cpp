// Dense structured-summary prompt for compaction handoff (replaces ad-hoc
// handoff prose with a consistent, information-dense template the model fills).
#include "../test_main.hpp"
#include "../../src/cli/dense_summary.hpp"
#include <string>
using namespace icmg::cli;

TEST("dense_summary: prompt carries all 5 structured sections") {
    std::string p = denseSummaryPrompt();
    ASSERT_TRUE(p.find("### Goal")  != std::string::npos);
    ASSERT_TRUE(p.find("### Done")  != std::string::npos);
    ASSERT_TRUE(p.find("### State") != std::string::npos);
    ASSERT_TRUE(p.find("### Next")  != std::string::npos);
    ASSERT_TRUE(p.find("### Keep")  != std::string::npos);
}

TEST("dense_summary: enforces density + token ceiling guidance") {
    std::string p = denseSummaryPrompt();
    ASSERT_TRUE(p.find("dense") != std::string::npos || p.find("DENSE") != std::string::npos);
    ASSERT_TRUE(p.find("1000") != std::string::npos);   // ~1000 token ceiling
}

TEST("dense_summary: interpolates turn + compaction counts") {
    std::string p = denseSummaryPrompt(42, 3);
    ASSERT_TRUE(p.find("42") != std::string::npos);
    ASSERT_TRUE(p.find("3")  != std::string::npos);
}

TEST("dense_summary: zero counts still render a valid prompt") {
    std::string p = denseSummaryPrompt(0, 0);
    ASSERT_TRUE(p.size() > 100);
    ASSERT_TRUE(p.find("### Goal") != std::string::npos);
}
