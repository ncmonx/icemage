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
    std::map<int64_t,double> deg{ {1, 9}, {2, 1} };
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
    std::map<int64_t,double> deg{ {1,5} };
    auto s = buildRepoSkeleton(nodes, deg, 1000);
    ASSERT_CONTAINS(s, std::string("core.cpp"));
    ASSERT_CONTAINS(s, std::string("doWork"));
}

TEST("repo skeleton: respects byte budget") {
    std::vector<GraphNode> nodes;
    std::map<int64_t,double> deg;
    for (int64_t i = 1; i <= 50; ++i) {
        nodes.push_back(file(i, "file" + std::to_string(i) + ".cpp"));
        deg[i] = (double)(50 - i);
    }
    auto s = buildRepoSkeleton(nodes, deg, 200);
    ASSERT_TRUE(s.size() <= 260);   // budget + small overrun for the always-included top
}

TEST("repo skeleton: empty -> empty") {
    std::vector<GraphNode> nodes;
    std::map<int64_t,double> deg;
    ASSERT_EQ(buildRepoSkeleton(nodes, deg, 100), std::string(""));
}

TEST("isVendoredPath: third_party / node_modules / windows sep are vendored") {
    ASSERT_TRUE(isVendoredPath("third_party/onnx/api.h"));
    ASSERT_TRUE(isVendoredPath("src/third_party/x.cpp"));
    ASSERT_TRUE(isVendoredPath("a/node_modules/b.js"));
    ASSERT_TRUE(isVendoredPath("C:\\proj\\third_party\\llama\\x.h"));
}

TEST("isVendoredPath: own source is not vendored (segment match, not substring)") {
    ASSERT_FALSE(isVendoredPath("src/graph/graph_centrality.hpp"));
    ASSERT_FALSE(isVendoredPath("src/vendored_notes.cpp"));   // 'vendor' substring, no /vendor/ segment
}

TEST("repo skeleton: excludes vendored files by default, keeps own code") {
    std::vector<GraphNode> nodes{ file(1,"third_party/onnx/api.h"), file(2,"src/core/db.cpp") };
    std::map<int64_t,double> score{ {1, 0.9}, {2, 0.1} };    // vendored scores higher
    auto s = buildRepoSkeleton(nodes, score, 1000);          // default excludeVendored=true
    ASSERT_TRUE(s.find("third_party") == std::string::npos); // vendored dropped
    ASSERT_CONTAINS(s, std::string("src/core/db.cpp"));      // own code kept
}

TEST("repo skeleton: include-vendored shows vendored too") {
    std::vector<GraphNode> nodes{ file(1,"third_party/onnx/api.h"), file(2,"src/core/db.cpp") };
    std::map<int64_t,double> score{ {1, 0.9}, {2, 0.1} };
    auto s = buildRepoSkeleton(nodes, score, 1000, false);   // excludeVendored=false
    ASSERT_CONTAINS(s, std::string("third_party"));
}
TEST("repo skeleton: rootPrefix scopes to the project tree") {
    std::vector<GraphNode> nodes{
        file(1,"D:/proj/icemage/src/core/db.cpp"),
        file(2,"D:/proj/other/src/x.cpp"),
    };
    std::map<int64_t,double> score{ {1, 0.1}, {2, 0.9} };   // outside-tree scores higher
    auto s = buildRepoSkeleton(nodes, score, 1000, true, "D:/proj/icemage");
    ASSERT_CONTAINS(s, std::string("db.cpp"));
    ASSERT_TRUE(s.find("other/src/x.cpp") == std::string::npos);
}

TEST("repo skeleton: empty rootPrefix = no scoping (case-insensitive prefix)") {
    std::vector<GraphNode> nodes{ file(1,"D:/Proj/IceMage/src/a.cpp") };
    std::map<int64_t,double> score{ {1, 0.5} };
    auto s = buildRepoSkeleton(nodes, score, 1000, true, "d:/proj/icemage");
    ASSERT_CONTAINS(s, std::string("a.cpp"));   // matched case-insensitively
}
TEST("isTestPath: tests/ dir and test_/_test/.test/.spec basenames") {
    ASSERT_TRUE(isTestPath("tests/graph/test_x.cpp"));
    ASSERT_TRUE(isTestPath("src/foo/test/bar.cpp"));
    ASSERT_TRUE(isTestPath("pkg/foo_test.go"));
    ASSERT_TRUE(isTestPath("ui/Button.test.tsx"));
    ASSERT_TRUE(isTestPath("ui/Button.spec.ts"));
    ASSERT_TRUE(isTestPath("a/__tests__/b.js"));
}

TEST("isTestPath: production source is not a test") {
    ASSERT_FALSE(isTestPath("src/graph/graph_centrality.hpp"));
    ASSERT_FALSE(isTestPath("src/latest_value.cpp"));   // 'test' substring, not a test file
}

TEST("repo skeleton: excludes test files by default, keeps source") {
    std::vector<GraphNode> nodes{ file(1,"tests/graph/test_edges.cpp"), file(2,"src/core/db.cpp") };
    std::map<int64_t,double> score{ {1, 0.9}, {2, 0.1} };   // test scores higher (edge noise)
    auto s = buildRepoSkeleton(nodes, score, 1000);          // default excludeTests=true
    ASSERT_TRUE(s.find("test_edges.cpp") == std::string::npos);
    ASSERT_CONTAINS(s, std::string("src/core/db.cpp"));
}

TEST("repo skeleton: includeTests=true shows tests") {
    std::vector<GraphNode> nodes{ file(1,"tests/graph/test_edges.cpp"), file(2,"src/core/db.cpp") };
    std::map<int64_t,double> score{ {1, 0.9}, {2, 0.1} };
    auto s = buildRepoSkeleton(nodes, score, 1000, true, "", true);   // excludeTests=false
    ASSERT_CONTAINS(s, std::string("test_edges.cpp"));
}
#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
