// TDD (2026-11-11): budget-shaping ladder for `icmg context` bodies
// (research item 4, after Rivet arXiv 2610.34967). Over budget, keep the
// richest rung that fits instead of a blind mid-symbol byte cut:
//   full -> prefix + skeleton of the rest -> skeleton -> line-boundary truncate
#include "../test_main.hpp"
#include "../../src/cli/budget_ladder.hpp"

using namespace icmg::cli;

static std::string body10() {
    std::string b;
    for (int i = 1; i <= 10; ++i) b += "line " + std::to_string(i) + " xxxxxxxxxxxxxxxxxxxx\n";
    return b;   // 10 lines x 28 bytes
}

TEST("ladder: under budget -> full body, untouched") {
    auto r = fitBodyToBudget(body10(), {{1, "int a()"}}, 10000);
    ASSERT_EQ(r.rung, std::string("full"));
    ASSERT_EQ(r.text, body10());
}

TEST("ladder: over budget keeps a prefix plus signatures of the cut-off symbols") {
    std::vector<LadderSig> sigs = {{2, "int alpha()"}, {6, "int beta()"}, {9, "int gamma()"}};
    auto r = fitBodyToBudget(body10(), sigs, 200);
    ASSERT_EQ(r.rung, std::string("prefix+skeleton"));
    ASSERT_TRUE(r.text.size() <= 200);
    ASSERT_CONTAINS(r.text, std::string("line 1 "));
    ASSERT_CONTAINS(r.text, std::string("int gamma()"));     // never cut off
    ASSERT_TRUE(r.text.find("line 10 ") == std::string::npos);
    ASSERT_TRUE(r.keptLines > 0 && r.keptLines < 10);
    // no partial lines: the prefix ends exactly at a newline
    ASSERT_CONTAINS(r.text, std::string("line " + std::to_string(r.keptLines) + " xxxxxxxxxxxxxxxxxxxx\n"));
}

TEST("ladder: tiny budget -> skeleton only, then line-boundary truncate") {
    std::vector<LadderSig> sigs = {{2, "int alpha()"}, {6, "int beta()"}};
    auto sk = fitBodyToBudget(body10(), sigs, 90);
    ASSERT_EQ(sk.rung, std::string("skeleton"));
    ASSERT_TRUE(sk.text.size() <= 90);
    ASSERT_CONTAINS(sk.text, std::string("int beta()"));

    auto tr = fitBodyToBudget(body10(), {}, 60);           // no symbols known
    ASSERT_EQ(tr.rung, std::string("truncate"));
    ASSERT_TRUE(tr.text.size() <= 60);
    ASSERT_TRUE(!tr.text.empty() && tr.text.back() == '\n');   // whole lines only
    ASSERT_EQ(tr.keptLines, 2);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
