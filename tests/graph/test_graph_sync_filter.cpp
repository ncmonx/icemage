// Incremental graph→memory sync filter (2026-06-14). `icmg graph update` was
// re-syncing ALL ~7000 file nodes (one BM25 query each) on every run, even when
// scan touched a single file → multi-minute updates. shouldSyncNode restricts
// the sync to the files the scanner actually changed.
#include "../test_main.hpp"
#include "../../src/graph/graph_sync_filter.hpp"

using namespace icmg::graph;

// ---- Test 1: non-file nodes never sync -------------------------------------
TEST("graph_sync_filter: symbol/non-file nodes are skipped") {
    std::set<std::string> changed{"a.cpp"};
    ASSERT_TRUE(shouldSyncNode("a.cpp#foo", /*is_file*/false, changed, /*incr*/true) == false);
    ASSERT_TRUE(shouldSyncNode("a.cpp#foo", /*is_file*/false, {}, /*incr*/false) == false);
}

// ---- Test 2: full sync (non-incremental) syncs every file node -------------
TEST("graph_sync_filter: full sync includes all file nodes") {
    ASSERT_TRUE(shouldSyncNode("a.cpp", true, {}, /*incr*/false) == true);
    ASSERT_TRUE(shouldSyncNode("z/deep/file.ts", true, {}, /*incr*/false) == true);
}

// ---- Test 3: incremental syncs ONLY changed file nodes ---------------------
TEST("graph_sync_filter: incremental syncs only changed files") {
    std::set<std::string> changed{"a.cpp", "b.cpp"};
    ASSERT_TRUE(shouldSyncNode("a.cpp", true, changed, /*incr*/true) == true);   // changed
    ASSERT_TRUE(shouldSyncNode("b.cpp", true, changed, /*incr*/true) == true);   // changed
    ASSERT_TRUE(shouldSyncNode("c.cpp", true, changed, /*incr*/true) == false);  // unchanged -> skip
}

// ---- Test 4: incremental with empty changed-set syncs nothing --------------
TEST("graph_sync_filter: incremental + empty changed-set skips all") {
    ASSERT_TRUE(shouldSyncNode("a.cpp", true, {}, /*incr*/true) == false);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
