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
