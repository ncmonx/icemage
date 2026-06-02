// v2.0.0 C1+C3: pure injection-governor selection (selectWorkingSet — budget knapsack,
// pinned-survive) + lost-in-the-middle ordering (orderUShaped — relevant at extrema).
// Model-free, unit-testable.
#include "../test_main.hpp"
#include "../../src/core/working_set.hpp"

#include <string>
#include <vector>

using namespace icmg::core;

static Source mk(const std::string& id, int tokens, double rel, int prio, bool pinned) {
    Source s; s.id = id; s.text = id; s.tokens = tokens;
    s.relevance = rel; s.priority = prio; s.pinned = pinned; return s;
}

// ---- C1: selectWorkingSet ----

TEST("selectWorkingSet: budget >= total keeps all, totalTokens summed") {
    std::vector<Source> c{mk("a",10,0.5,1,false), mk("b",20,0.9,1,false)};
    auto ws = selectWorkingSet(c, 1000);
    ASSERT_EQ(ws.items.size(), (size_t)2);
    ASSERT_EQ(ws.totalTokens, 30);
}

TEST("selectWorkingSet: tight budget keeps highest priority x relevance") {
    std::vector<Source> c{mk("low",20,0.1,1,false), mk("HIGH",20,0.9,1,false)};
    auto ws = selectWorkingSet(c, 20);
    ASSERT_EQ(ws.items.size(), (size_t)1);
    ASSERT_EQ(ws.items[0].id, std::string("HIGH"));
}

TEST("selectWorkingSet: pinned survives even over budget") {
    std::vector<Source> c{mk("PIN",100,0.01,1,true), mk("x",10,0.9,1,false)};
    auto ws = selectWorkingSet(c, 5);  // budget too small for either
    bool hasPin = false;
    for (auto& s : ws.items) if (s.id == "PIN") hasPin = true;
    ASSERT_TRUE(hasPin);
}

TEST("selectWorkingSet: higher priority beats higher relevance") {
    // prio 2 x rel 0.3 (=0.6) beats prio 1 x rel 0.5 (=0.5)
    std::vector<Source> c{mk("P1",20,0.5,1,false), mk("P2",20,0.3,2,false)};
    auto ws = selectWorkingSet(c, 20);
    ASSERT_EQ(ws.items[0].id, std::string("P2"));
}

TEST("selectWorkingSet: empty input -> empty") {
    std::vector<Source> c;
    auto ws = selectWorkingSet(c, 100);
    ASSERT_EQ(ws.items.size(), (size_t)0);
    ASSERT_EQ(ws.totalTokens, 0);
}

// ---- C3: orderUShaped ----

TEST("orderUShaped: most relevant at front+back, filler in middle") {
    // relevances 0.5,0.9,0.1,0.7,0.3 -> front=0.9, back=0.7, center=0.1
    std::vector<Source> in{mk("r5",1,0.5,1,false), mk("r9",1,0.9,1,false),
                           mk("r1",1,0.1,1,false), mk("r7",1,0.7,1,false),
                           mk("r3",1,0.3,1,false)};
    auto out = orderUShaped(in);
    ASSERT_EQ(out.size(), (size_t)5);
    ASSERT_EQ(out.front().id, std::string("r9"));   // highest at front
    ASSERT_EQ(out.back().id,  std::string("r7"));   // 2nd-highest at back
    ASSERT_EQ(out[2].id,      std::string("r1"));   // lowest in dead-center
}

TEST("orderUShaped: single item unchanged") {
    std::vector<Source> in{mk("solo",1,0.5,1,false)};
    auto out = orderUShaped(in);
    ASSERT_EQ(out.size(), (size_t)1);
    ASSERT_EQ(out[0].id, std::string("solo"));
}

TEST("orderUShaped: empty unchanged") {
    std::vector<Source> in;
    ASSERT_EQ(orderUShaped(in).size(), (size_t)0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
