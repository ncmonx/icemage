// Active prompt-history reuse: pure word-set Jaccard scorer + confidence-gated
// suggest(). A new prompt that closely matches a stored one surfaces the past
// response (hot-path reuse) instead of re-deriving; below the gate -> no match.
// NOTE: mono-test shares one process + temp DB persists; DISTINCT user_id per TEST.
#include "../test_main.hpp"
#include "../../src/core/prompt_history.hpp"
#include "../../src/core/db.hpp"
#include <string>
using namespace icmg::core;

static std::string sugDb() { return std::string("prompt_suggest_test.db"); }

TEST("promptJaccard: identical prompts score 1.0") {
    double s = promptJaccard("how to build the project", "how to build the project");
    ASSERT_TRUE(s > 0.999);
}

TEST("promptJaccard: disjoint prompts score 0.0") {
    double s = promptJaccard("compile vulkan shaders", "delete user rows");
    ASSERT_TRUE(s < 0.001);
}

TEST("promptJaccard: partial overlap is between 0 and 1") {
    // tokens(len>=3): {how(skip<3 no, 'how'=3 ok), build, the(skip), project} vs same + extra
    double s = promptJaccard("how to build the project", "how to build the docker project now");
    ASSERT_TRUE(s > 0.0 && s < 1.0);
}

TEST("promptJaccard: order independent") {
    double a = promptJaccard("build vulkan shader", "shader vulkan build");
    ASSERT_TRUE(a > 0.999);
}

TEST("suggest: near-duplicate prompt surfaces past response above gate") {
    Db db(sugDb());
    PromptHistory ph(db);
    ph.record("u_sug_hit", "work", "how do I rebuild the msvc icmg binary",
              "run: pwsh -File build.ps1 -Target icmg");
    auto s = ph.suggest("u_sug_hit", "how do I rebuild the msvc icmg binary please", 0.4, 25);
    ASSERT_TRUE(s.found);
    ASSERT_EQ(s.row.response, std::string("run: pwsh -File build.ps1 -Target icmg"));
    ASSERT_TRUE(s.score >= 0.4);
}

TEST("suggest: unrelated prompt returns no match (below gate)") {
    Db db(sugDb());
    PromptHistory ph(db);
    ph.record("u_sug_miss", "work", "how do I rebuild the msvc icmg binary",
              "run: pwsh -File build.ps1 -Target icmg");
    auto s = ph.suggest("u_sug_miss", "what color is the sky today", 0.4, 25);
    ASSERT_TRUE(!s.found);
}

TEST("suggest: internal _-prefixed zones are never surfaced as reuse") {
    Db db(sugDb());
    PromptHistory ph(db);
    // a prompt stored in an internal zone (e.g. the passphrase machinery)
    ph.record("u_sug_intz", "_passphrase", "sudah makan", "siang tadi aku sudah tidur");
    // a closely matching query must NOT reuse the internal-zone answer
    auto s = ph.suggest("u_sug_intz", "sudah makan belum", 0.3, 25);
    ASSERT_TRUE(!s.found);
}

TEST("suggest: normal zones still surface alongside internal ones excluded") {
    Db db(sugDb());
    PromptHistory ph(db);
    ph.record("u_sug_mix", "_mode", "current mode banner text here", "internal");
    ph.record("u_sug_mix", "work", "how do I deploy the service", "kubectl apply");
    auto s = ph.suggest("u_sug_mix", "how do I deploy the service now", 0.4, 25);
    ASSERT_TRUE(s.found);
    ASSERT_EQ(s.row.response, std::string("kubectl apply"));   // not the _mode entry
}

TEST("suggest: picks the highest-scoring of several candidates") {
    Db db(sugDb());
    PromptHistory ph(db);
    ph.record("u_sug_best", "work", "build the project with cmake", "answer-A");
    ph.record("u_sug_best", "work", "build the project with cmake and ninja fast", "answer-B");
    // Query is closest to the first (exact token set) -> answer-A.
    auto s = ph.suggest("u_sug_best", "build the project with cmake", 0.3, 25);
    ASSERT_TRUE(s.found);
    ASSERT_EQ(s.row.response, std::string("answer-A"));
}
