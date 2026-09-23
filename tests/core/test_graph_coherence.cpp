// TDD (2026-09-23): graph-coherence pull for salience compression -- IDE-E
// from the landscape scan (TopoCompress, arXiv 2608.30811: training-free
// compression that keeps GRAPH-WIRED spans together instead of scoring lines
// independently). When a kept line cites file A and a dropped line cites file
// B that is graph-adjacent to A, the dropped line is structural context for
// the kept one -- pull it back. Pure ops; adjacency is fed by the cmd layer.
#include "../test_main.hpp"
#include "../../src/core/graph_coherence.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace icmg::core;

using Adj = std::map<std::string, std::set<std::string>>;

// 1. Dropped line citing a file adjacent to a kept-cited file is pulled back.
TEST("coherence: adjacent citation pulled back") {
    std::vector<std::string> lines = {
        "fix applied in dispatcher.cpp for routing",     // kept
        "registry.hpp defines the lookup table",         // dropped, adjacent
        "the weather was nice that day",                 // dropped, no citation
    };
    std::vector<bool> keep = {true, false, false};
    Adj adj = {{"dispatcher.cpp", {"registry.hpp"}},
               {"registry.hpp", {"dispatcher.cpp"}}};
    auto pulls = graphCoherencePulls(lines, keep, adj, 10);
    ASSERT_EQ((int)pulls.size(), 1);
    ASSERT_EQ((int)pulls[0], 1);
}

// 2. Non-adjacent citations stay dropped.
TEST("coherence: unrelated citation stays dropped") {
    std::vector<std::string> lines = {
        "fix applied in dispatcher.cpp",
        "notes about scanner.cpp internals",   // cited but NOT adjacent
    };
    std::vector<bool> keep = {true, false};
    Adj adj = {{"dispatcher.cpp", {"registry.hpp"}}};
    auto pulls = graphCoherencePulls(lines, keep, adj, 10);
    ASSERT_EQ((int)pulls.size(), 0);
}

// 3. Pull budget is respected (strongest-first not required; just capped).
TEST("coherence: pull budget capped") {
    std::vector<std::string> lines = {
        "root cause in db.hpp",
        "config.hpp reads the settings",
        "migrator.hpp applies migrations",
        "registry.hpp registers commands",
    };
    std::vector<bool> keep = {true, false, false, false};
    Adj adj = {{"db.hpp", {"config.hpp", "migrator.hpp", "registry.hpp"}}};
    auto pulls = graphCoherencePulls(lines, keep, adj, 2);
    ASSERT_EQ((int)pulls.size(), 2);
}

// 4. Lines without citations and empty adjacency are safe no-ops.
TEST("coherence: no citations or empty graph -> no pulls") {
    std::vector<std::string> lines = {"plain prose", "more prose"};
    std::vector<bool> keep = {true, false};
    auto pulls = graphCoherencePulls(lines, keep, {}, 10);
    ASSERT_EQ((int)pulls.size(), 0);
}

// 5. Already-kept lines are never returned as pulls.
TEST("coherence: kept lines not re-pulled") {
    std::vector<std::string> lines = {
        "dispatcher.cpp routes commands",
        "registry.hpp holds the table",
    };
    std::vector<bool> keep = {true, true};
    Adj adj = {{"dispatcher.cpp", {"registry.hpp"}}};
    auto pulls = graphCoherencePulls(lines, keep, adj, 10);
    ASSERT_EQ((int)pulls.size(), 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
