// v2.0.0 Phase 0b TDD: graph prune — select graph nodes whose backing file is
// gone so `icmg graph prune --missing` can delete scan-pollution (dead temp /
// build / AppData .node files left in the default zone). known-issue #30469.
//
// Verifies the PURE selector (no filesystem) — existence is injected:
//   - file node whose file exists      → NOT selected
//   - file node whose file is missing  → selected
//   - symbol-level node (kind != file) → never selected (no 1:1 disk file)
//   - empty-path node                  → never selected
//   - returns every missing file path, order-independent membership

#include "../test_main.hpp"
#include "../../src/graph/graph_prune.hpp"
#include "../../src/graph/graph_node.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace icmg;

namespace {

graph::GraphNode mkFile(const std::string& path) {
    graph::GraphNode n;
    n.path = path;
    n.kind = "file";
    return n;
}

graph::GraphNode mkSymbol(const std::string& path, const std::string& sym) {
    graph::GraphNode n;
    n.path = path;
    n.kind = "function";
    n.symbol_name = sym;
    return n;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

TEST("graph prune: existing file not selected") {
    std::vector<graph::GraphNode> nodes{ mkFile("src/core/db.cpp") };
    std::set<std::string> present{ "src/core/db.cpp" };
    auto gone = graph::selectMissingNodes(nodes,
        [&](const std::string& p){ return present.count(p) > 0; });
    ASSERT_EQ((int)gone.size(), 0);
}

TEST("graph prune: missing file selected") {
    std::vector<graph::GraphNode> nodes{ mkFile("C:/Temp/.daba123-00000000.node") };
    auto gone = graph::selectMissingNodes(nodes,
        [](const std::string&){ return false; });   // nothing exists
    ASSERT_EQ((int)gone.size(), 1);
    ASSERT_TRUE(contains(gone, "C:/Temp/.daba123-00000000.node"));
}

TEST("graph prune: symbol node never pruned even if path missing") {
    std::vector<graph::GraphNode> nodes{ mkSymbol("gone/file.cpp", "doThing") };
    auto gone = graph::selectMissingNodes(nodes,
        [](const std::string&){ return false; });
    ASSERT_EQ((int)gone.size(), 0);
}

TEST("graph prune: empty path skipped") {
    std::vector<graph::GraphNode> nodes{ mkFile("") };
    auto gone = graph::selectMissingNodes(nodes,
        [](const std::string&){ return false; });
    ASSERT_EQ((int)gone.size(), 0);
}

TEST("graph prune: mixed set selects only missing files") {
    std::vector<graph::GraphNode> nodes{
        mkFile("src/main.cpp"),                 // exists
        mkFile("build/NUL"),                    // missing
        mkFile("C:/Temp/.dabaXYZ.node"),        // missing
        mkSymbol("src/main.cpp", "main"),       // symbol, skip
    };
    std::set<std::string> present{ "src/main.cpp" };
    auto gone = graph::selectMissingNodes(nodes,
        [&](const std::string& p){ return present.count(p) > 0; });
    ASSERT_EQ((int)gone.size(), 2);
    ASSERT_TRUE(contains(gone, "build/NUL"));
    ASSERT_TRUE(contains(gone, "C:/Temp/.dabaXYZ.node"));
    ASSERT_FALSE(contains(gone, "src/main.cpp"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
