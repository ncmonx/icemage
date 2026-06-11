// Feature-coverage scorecard — pure-core unit tests (TDD-first, 2026-06-11).

#include "../test_main.hpp"
#include "../../src/cli/discipline_score.hpp"

using namespace icmg::cli;

TEST("discipline: empty session -> 0%, all cold") {
    auto s = scoreDiscipline({});
    ASSERT_EQ(s.used, 0);
    ASSERT_EQ(s.pct, 0);
    ASSERT_EQ((int)s.cold.size(), s.total);
}

TEST("discipline: all core used -> 100%, none cold") {
    std::set<std::string> all(disciplineCoreFeatures().begin(),
                              disciplineCoreFeatures().end());
    auto s = scoreDiscipline(all);
    ASSERT_EQ(s.used, s.total);
    ASSERT_EQ(s.pct, 100);
    ASSERT_TRUE(s.cold.empty());
}

TEST("discipline: partial -> correct pct + cold list") {
    auto s = scoreDiscipline({"recall", "context", "run"});
    ASSERT_EQ(s.used, 3);
    ASSERT_EQ(s.total, 12);
    ASSERT_EQ(s.pct, 25);                 // 3/12
    ASSERT_EQ((int)s.cold.size(), 9);
}

TEST("discipline: non-core commands ignored (don't inflate score)") {
    auto s = scoreDiscipline({"recall", "doctor", "health", "ls"});
    ASSERT_EQ(s.used, 1);                 // only 'recall' is core
    ASSERT_EQ(s.pct, 8);                  // 1/12 = 8
}

TEST("discipline: cold list preserves core order") {
    auto s = scoreDiscipline({"recall"});  // recall is first in core
    ASSERT_EQ(s.cold.front(), std::string("pack"));   // next core after recall
    ASSERT_EQ(s.cold.back(),  std::string("memoir")); // last core
}

TEST("discipline: custom core list honored") {
    std::vector<std::string> core{"a", "b", "c", "d"};
    auto s = scoreDiscipline({"b", "d"}, core);
    ASSERT_EQ(s.total, 4);
    ASSERT_EQ(s.used, 2);
    ASSERT_EQ(s.pct, 50);
}

TEST("discipline: grade bands (strong/ok/thin/blind)") {
    ASSERT_EQ(disciplineGrade(100), std::string("strong"));
    ASSERT_EQ(disciplineGrade(75),  std::string("strong"));
    ASSERT_EQ(disciplineGrade(74),  std::string("ok"));
    ASSERT_EQ(disciplineGrade(50),  std::string("ok"));
    ASSERT_EQ(disciplineGrade(49),  std::string("thin"));
    ASSERT_EQ(disciplineGrade(25),  std::string("thin"));
    ASSERT_EQ(disciplineGrade(24),  std::string("blind"));
    ASSERT_EQ(disciplineGrade(0),   std::string("blind"));
}
