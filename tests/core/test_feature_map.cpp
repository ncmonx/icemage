// Feature-map: neighborsOf -- a command's derived "you-are-here" neighbors.
// Reuses rankCommands over the live registry docs (name+desc); zero new data,
// so the map never rots. Self is excluded; unknown cmd falls back to intent.
#include "../test_main.hpp"
#include "../../src/core/command_suggest.hpp"
#include <string>
using namespace icmg::core;

static std::vector<CmdDoc> docs() {
    return { {"context-budget","show context window token usage"},
             {"savings","token savings report"},
             {"govern","context budget governor token injection"},
             {"graph","code graph symbols"},
             {"zone","subsystem zone tagging"} };
}

TEST("feature_map: neighborsOf excludes self + ranks similar first") {
    auto n = neighborsOf("context-budget", docs(), 3);
    ASSERT_TRUE(n.size() <= 3);
    for (auto& h : n) ASSERT_TRUE(h.name != std::string("context-budget")); // self excluded
    // token/budget-related neighbors should appear (savings/govern share words)
    bool sawTokenish = false;
    for (auto& h : n) if (h.name == "savings" || h.name == "govern") sawTokenish = true;
    ASSERT_TRUE(sawTokenish);
}

TEST("feature_map: unknown cmd -> intent fallback never crashes") {
    auto n = neighborsOf("nonexistent-xyz", docs(), 3);
    ASSERT_TRUE(n.size() <= 3);   // ranks the string as a free intent; may be empty
}

TEST("feature_map: empty docs -> empty") {
    ASSERT_EQ(neighborsOf("x", {}, 3).size(), (size_t)0);
}

TEST("feature_map: formatRelatedFooter empty neighbors -> empty string") {
    ASSERT_EQ(formatRelatedFooter("ctx", {}).size(), (size_t)0);
}

TEST("feature_map: formatRelatedFooter lists neighbors + map hint") {
    std::vector<CmdHit> nb = { {"savings", 0.9}, {"govern", 0.5} };
    std::string f = formatRelatedFooter("context-budget", nb);
    ASSERT_TRUE(f.find("related:") != std::string::npos);
    ASSERT_TRUE(f.find("icmg savings") != std::string::npos);
    ASSERT_TRUE(f.find("icmg govern") != std::string::npos);
    ASSERT_TRUE(f.find("icmg map context-budget") != std::string::npos);
}

TEST("feature_map: shouldShowFooter --help on by default, suppressed by env") {
    ASSERT_TRUE(shouldShowFooter(/*isHelp*/true,  /*rcOk*/true,  /*noHelp*/false, /*optIn*/false));
    ASSERT_TRUE(shouldShowFooter(true,  false, false, false));   // help shows even on nonzero rc
    ASSERT_TRUE(!shouldShowFooter(true, true,  /*noHelp*/true,  false)); // ICMG_NO_MAP_FOOTER suppresses
}

TEST("feature_map: shouldShowFooter normal run off unless opt-in + success") {
    ASSERT_TRUE(!shouldShowFooter(/*isHelp*/false, true,  false, /*optIn*/false)); // default off
    ASSERT_TRUE( shouldShowFooter(false, /*rcOk*/true,  false, /*optIn*/true));    // opt-in + success
    ASSERT_TRUE(!shouldShowFooter(false, /*rcOk*/false, false, /*optIn*/true));    // opt-in but failed -> off
}

TEST("feature_map: findNearDuplicateCommands flags overlapping pair") {
    std::vector<CmdDoc> d = {
        {"context-budget", "show context window token usage from transcript"},
        {"ctxbudget",      "show context window token usage from transcript"}, // near-dup
        {"graph",          "code graph symbols and edges"},
    };
    auto dups = findNearDuplicateCommands(d, 0.5);
    ASSERT_TRUE(!dups.empty());
    // the duplicated pair must be the budget twins, not graph
    ASSERT_TRUE((dups[0].a == "context-budget" && dups[0].b == "ctxbudget") ||
                (dups[0].a == "ctxbudget" && dups[0].b == "context-budget"));
}

TEST("feature_map: findNearDuplicateCommands clean set -> empty") {
    std::vector<CmdDoc> d = {
        {"graph", "code graph symbols"},
        {"zone",  "subsystem tagging"},
        {"fetch", "download a url with cache"},
    };
    ASSERT_EQ(findNearDuplicateCommands(d, 0.5).size(), (size_t)0);
}
