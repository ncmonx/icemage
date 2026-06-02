// v2.0.0 C4: lossless compaction transition — snapshot the working-set manifest before
// compaction, rebuild a HARD-CAPPED pinned-only anchor after (F2 anti-thrash). Pure.
#include "../test_main.hpp"
#include "../../src/core/ws_snapshot.hpp"
#include "../../src/core/working_set.hpp"
#include <string>
#include <vector>

using namespace icmg::core;

static Source mk(const std::string& id, int tokens, bool pinned) {
    Source s; s.id = id; s.text = id; s.tokens = tokens; s.pinned = pinned; return s;
}

TEST("snapshotManifest: captures all ids + pinned subset") {
    WorkingSet ws;
    ws.items = {mk("a",10,false), mk("PIN",20,true), mk("b",5,false)};
    auto m = snapshotManifest(ws);
    ASSERT_EQ(m.nodeIds.size(), (size_t)3);
    ASSERT_EQ(m.pinnedIds.size(), (size_t)1);
    ASSERT_EQ(m.pinnedIds[0], std::string("PIN"));
}

TEST("rebuildFromManifest: emits pinned only, within hard cap") {
    Manifest m;
    m.nodeIds = {"a","PIN1","b","PIN2"};
    m.pinnedIds = {"PIN1","PIN2"};
    // each pinned costs 100 tokens here; cap 150 fits exactly 1.
    std::vector<Source> live = {mk("PIN1",100,true), mk("PIN2",100,true), mk("a",10,false)};
    auto ws = rebuildFromManifest(m, 150, live);
    ASSERT_EQ(ws.items.size(), (size_t)1);
    ASSERT_TRUE(ws.items[0].pinned);
}

TEST("rebuildFromManifest: drops non-pinned even if in manifest") {
    Manifest m; m.nodeIds = {"a","b"}; m.pinnedIds = {};
    std::vector<Source> live = {mk("a",10,false), mk("b",10,false)};
    auto ws = rebuildFromManifest(m, 1000, live);
    ASSERT_EQ(ws.items.size(), (size_t)0);  // nothing pinned -> nothing re-anchored
}

TEST("rebuildFromManifest: missing live source skipped (fresh-fetch model)") {
    Manifest m; m.nodeIds = {"GONE"}; m.pinnedIds = {"GONE"};
    std::vector<Source> live = {mk("other",10,true)};
    auto ws = rebuildFromManifest(m, 1000, live);
    ASSERT_EQ(ws.items.size(), (size_t)0);  // GONE not in live -> skipped, no stale text
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
