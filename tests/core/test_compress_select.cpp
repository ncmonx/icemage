// v2.0.0 TE2 (token-efficiency v2): salience compression backend — keep the most
// informative spans within a char budget, preserving original order. This is the
// pure, model-free selection core (LLMLingua-style coarse-to-fine); the per-span
// score is pluggable (heuristic now, llama-logprob perplexity later).

#include "../test_main.hpp"
#include "../../src/core/compress_select.hpp"

#include <string>
#include <vector>

using namespace icmg::core;

TEST("selectByBudget: budget >= total keeps everything in order") {
    std::vector<std::string> sp{"alpha", "beta", "gamma"};
    std::vector<double> sc{0.1, 0.9, 0.5};
    auto out = selectByBudget(sp, sc, 1000, "\n");
    ASSERT_EQ(out, std::string("alpha\nbeta\ngamma"));
}

TEST("selectByBudget: tight budget keeps highest-score spans, original order") {
    std::vector<std::string> sp{"low1", "HIGH", "low2", "MID"};
    std::vector<double> sc{0.1, 0.9, 0.2, 0.6};
    // budget fits ~2 spans (4 chars each + sep). Keep HIGH + MID, drop low1/low2.
    auto out = selectByBudget(sp, sc, 9, "\n");
    ASSERT_EQ(out, std::string("HIGH\nMID"));
}

TEST("selectByBudget: empty input -> empty") {
    std::vector<std::string> sp;
    std::vector<double> sc;
    ASSERT_EQ(selectByBudget(sp, sc, 100, "\n"), std::string(""));
}

TEST("selectByBudget: zero budget still keeps the single highest (never empty-out)") {
    std::vector<std::string> sp{"a", "bWINNER", "c"};
    std::vector<double> sc{0.2, 0.99, 0.3};
    auto out = selectByBudget(sp, sc, 0, "\n");
    ASSERT_EQ(out, std::string("bWINNER"));
}

TEST("infoScore: identifier/rare-dense line scores above boilerplate") {
    double code = infoScore("resolveAndInsertEdges(node.id, target_path);");
    double boiler = infoScore("    // ----------------------------------");
    ASSERT_TRUE(code > boiler);
}

TEST("infoScore: blank / pure-punctuation line ~0") {
    ASSERT_TRUE(infoScore("        ") < 0.05);
    ASSERT_TRUE(infoScore("});") < 0.5);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
