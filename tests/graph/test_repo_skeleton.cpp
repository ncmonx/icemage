// v2.0.0 externals (Ultra Compression / repo-compact): a token-budgeted repo
// skeleton — most-connected files first, each with its symbol signatures — so an
// agent onboards a whole repo from one dense view instead of reading many files.
// Pure (no DB) so it is unit-testable.

#include "../test_main.hpp"
#include "../../src/graph/repo_skeleton.hpp"
#include "../../src/graph/graph_node.hpp"

#include <map>
#include <string>
#include <vector>

using namespace icmg::graph;

namespace {
GraphNode file(int64_t id, const std::string& path) {
    GraphNode n; n.id = id; n.path = path; n.kind = "file"; return n;
}
GraphNode sym(int64_t id, int64_t parent, const std::string& path,
              const std::string& name, const std::string& sig) {
    GraphNode n; n.id = id; n.parent_id = parent; n.path = path;
    n.kind = "function"; n.symbol_name = name; n.signature = sig; return n;
}
}  // namespace

TEST("repo skeleton: most-connected file emitted first") {
    std::vector<GraphNode> nodes{ file(1,"core.cpp"), file(2,"util.cpp") };
    std::map<int64_t,int> deg{ {1, 9}, {2, 1} };
    auto s = buildRepoSkeleton(nodes, deg, 1000);
    auto pc = s.find("core.cpp");
    auto pu = s.find("util.cpp");
    ASSERT_TRUE(pc != std::string::npos && pu != std::string::npos);
    ASSERT_TRUE(pc < pu);   // higher centrality first
}

TEST("repo skeleton: includes child symbol signatures") {
    std::vector<GraphNode> nodes{
        file(1,"core.cpp"),
        sym(2,1,"core.cpp","doWork","void doWork(int)"),
    };
    std::map<int64_t,int> deg{ {1,5} };
    auto s = buildRepoSkeleton(nodes, deg, 1000);
    ASSERT_CONTAINS(s, std::string("core.cpp"));
    ASSERT_CONTAINS(s, std::string("doWork"));
}

TEST("repo skeleton: respects byte budget") {
    std::vector<GraphNode> nodes;
    std::map<int64_t,int> deg;
    for (int64_t i = 1; i <= 50; ++i) {
        nodes.push_back(file(i, "file" + std::to_string(i) + ".cpp"));
        deg[i] = (int)(50 - i);
    }
    auto s = buildRepoSkeleton(nodes, deg, 200);
    ASSERT_TRUE(s.size() <= 260);   // budget + small overrun for the always-included top
}

TEST("repo skeleton: empty -> empty") {
    std::vector<GraphNode> nodes;
    std::map<int64_t,int> deg;
    ASSERT_EQ(buildRepoSkeleton(nodes, deg, 100), std::string(""));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
