// v1.63 F8: mine heuristic logic (pure). The command + LLM path are
// smoke-tested; here we verify the deterministic suggestion logic.

#include "../test_main.hpp"
#include "../../src/cli/mine_logic.hpp"

using namespace icmg;
using icmg::cli::topicPrefix;
using icmg::cli::heuristicMine;

namespace {
imem::MemoryNode mk(const std::string& topic) {
    imem::MemoryNode n; n.topic = topic; n.content = "x"; return n;
}
}

TEST("mine: topicPrefix splits on - and :") {
    ASSERT_EQ(topicPrefix("decisions-db"), std::string("decisions"));
    ASSERT_EQ(topicPrefix("bug:linker"), std::string("bug"));
    ASSERT_EQ(topicPrefix("plain"), std::string("plain"));
}

TEST("mine: recurring prefix (>=2) surfaces as suggestion") {
    std::vector<imem::MemoryNode> nodes = {
        mk("decisions-db"), mk("decisions-ui"), mk("decisions-cache"),
        mk("bug:once")
    };
    std::string out = heuristicMine(nodes);
    ASSERT_TRUE(out.find("decisions") != std::string::npos);
    ASSERT_TRUE(out.find("3 entries") != std::string::npos);
    // singleton 'bug' (count 1) must NOT be suggested
    ASSERT_TRUE(out.find("\"bug\"") == std::string::npos);
    // always tagged suggestions-only
    ASSERT_TRUE(out.find("nothing applied") != std::string::npos);
}

TEST("mine: no recurring prefix -> guidance, not a rule") {
    std::vector<imem::MemoryNode> nodes = { mk("a-1"), mk("b-2"), mk("c-3") };
    std::string out = heuristicMine(nodes);
    ASSERT_TRUE(out.find("no recurring topic prefix") != std::string::npos);
}

TEST("mine: empty nodes -> guidance line, no crash") {
    std::vector<imem::MemoryNode> nodes;
    std::string out = heuristicMine(nodes);
    ASSERT_TRUE(out.find("no recurring") != std::string::npos);
}

TEST("mine: caps at 5 suggestions") {
    std::vector<imem::MemoryNode> nodes;
    // 7 distinct prefixes each appearing twice -> only 5 shown.
    for (char c = 'a'; c <= 'g'; ++c) {
        std::string pre(1, c);
        nodes.push_back(mk(pre + "-x"));
        nodes.push_back(mk(pre + "-y"));
    }
    std::string out = heuristicMine(nodes);
    int dashes = 0;
    size_t pos = 0;
    while ((pos = out.find("  - consider", pos)) != std::string::npos) { ++dashes; pos += 5; }
    ASSERT_TRUE(dashes == 5);
}
