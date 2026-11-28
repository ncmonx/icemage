// Incremental cross-reference (xref) edge rebuild filter (2026-06-14).
// `icmg graph update` always called GraphStore::buildXRefEdges(), which reads
// the FULL content of EVERY graph node (~7000 files) and, per file, scans for
// every declared class name -> O(nodes * classes * filelen) + ~7000 disk reads
// on EVERY run, regardless of how many files actually changed. That was the
// ~150s bottleneck remaining after the mem-sync fix (it ran even at memory+0).
//
// These pure predicates let `graph update` (incremental) skip xref entirely
// when nothing changed, and restrict the source-node read+scan to the files the
// scanner actually touched. Full `graph scan` (non-incremental) keeps the old
// whole-graph behavior.
#include "../test_main.hpp"
#include "../../src/graph/graph_xref_filter.hpp"

using namespace icmg::graph;

// ---- Test 1: full (non-incremental) xref always runs -----------------------
TEST("graph_xref_filter: non-incremental always runs") {
    ASSERT_TRUE(xrefShouldRun(/*changed_count*/0, /*incremental*/false) == true);
    ASSERT_TRUE(xrefShouldRun(5, false) == true);
}

// ---- Test 2: incremental + nothing changed -> skip entirely ----------------
TEST("graph_xref_filter: incremental + empty changed-set skips") {
    ASSERT_TRUE(xrefShouldRun(0, /*incremental*/true) == false);
}

// ---- Test 3: incremental + some changed -> runs ----------------------------
TEST("graph_xref_filter: incremental + changed files runs") {
    ASSERT_TRUE(xrefShouldRun(1, true) == true);
    ASSERT_TRUE(xrefShouldRun(42, true) == true);
}

// ---- Test 4: full scan reads every source node -----------------------------
TEST("graph_xref_filter: non-incremental reads every source node") {
    std::set<std::string> changed{"a.cpp"};
    ASSERT_TRUE(xrefIsSourceNode("a.cpp", changed, /*incremental*/false) == true);
    ASSERT_TRUE(xrefIsSourceNode("untouched.cpp", changed, false) == true);
}

// ---- Test 5: incremental reads only changed source nodes -------------------
TEST("graph_xref_filter: incremental reads only changed source nodes") {
    std::set<std::string> changed{"a.cpp", "b.cpp"};
    ASSERT_TRUE(xrefIsSourceNode("a.cpp", changed, /*incremental*/true) == true);
    ASSERT_TRUE(xrefIsSourceNode("b.cpp", changed, true) == true);
    ASSERT_TRUE(xrefIsSourceNode("c.cpp", changed, true) == false);  // unchanged -> skip read
}

// ---- Scope (2026-11-28): no edges to vendored / out-of-root files ----------
TEST("graph_xref_filter: in-scope is root-prefix by segment, slash/case blind") {
    const std::string root = "D:\\Work\\icemage";
    ASSERT_TRUE(xrefInScope("D:\\Work\\icemage\\src\\a.cpp", root));
    ASSERT_TRUE(xrefInScope("d:/work/ICEMAGE/src/a.cpp", root));
    ASSERT_TRUE(!xrefInScope("D:\\Work\\icemage-wt2\\src\\a.cpp", root));   // sibling repo
    ASSERT_TRUE(!xrefInScope("D:\\Work\\helper code\\x.ts", root));
    ASSERT_TRUE(xrefInScope("anything", ""));                                // no root -> no limit
}

TEST("graph_xref_filter: project code never 'uses' vendored code by word") {
    const std::string r = "/p";
    ASSERT_TRUE(!xrefEdgeAllowed("/p/src/find.hpp", "/p/third_party/onnx/api.h", r));
    ASSERT_TRUE(!xrefEdgeAllowed("/p/src/a.ts", "/p/node_modules/x/i.d.ts", r));
    ASSERT_TRUE(xrefEdgeAllowed("/p/third_party/llama/a.c", "/p/third_party/llama/b.h", r));
    ASSERT_TRUE(xrefEdgeAllowed("/p/src/a.cpp", "/p/src/b.hpp", r));
    ASSERT_TRUE(!xrefEdgeAllowed("/p/third_party/lib/api.h", "/p/src/store.hpp", r)); // reverse too
    ASSERT_TRUE(!xrefEdgeAllowed("/p/src/a.cpp", "/q/src/b.hpp", r));
}

TEST("graph_xref_filter: names declared in many files carry no signal") {
    ASSERT_TRUE(xrefNameUseful("GraphStore", 1));
    ASSERT_TRUE(xrefNameUseful("Config", kXrefMaxDeclarers));
    ASSERT_TRUE(!xrefNameUseful("Config", kXrefMaxDeclarers + 1));
    ASSERT_TRUE(!xrefNameUseful("Id", 1));                                   // too short
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
