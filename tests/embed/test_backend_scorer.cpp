// M8 T8: backend routing scorer tests.
#include "../test_main.hpp"
#include "../../src/embed/backend_scorer.hpp"
#include <vector>

using icmg::embed::BackendScore;
using icmg::embed::selectBest;

TEST("backend_scorer: lower latency scores higher") {
    BackendScore fast; fast.name = "fast"; fast.latency_ms = 50;  fast.call_count = 1;
    BackendScore slow; slow.name = "slow"; slow.latency_ms = 4000; slow.call_count = 1;
    ASSERT_TRUE(fast.score() > slow.score());
}

TEST("backend_scorer: high error rate penalizes score") {
    BackendScore reliable; reliable.latency_ms = 100; reliable.error_rate = 0.0; reliable.call_count = 1;
    BackendScore flaky;    flaky.latency_ms = 100;    flaky.error_rate = 0.5;    flaky.call_count = 1;
    ASSERT_TRUE(reliable.score() > flaky.score());
}

TEST("backend_scorer: selectBest picks highest score") {
    std::vector<BackendScore> backends(3);
    backends[0].name = "a"; backends[0].latency_ms = 2000; backends[0].call_count = 1;
    backends[1].name = "b"; backends[1].latency_ms = 100;  backends[1].call_count = 1; // best
    backends[2].name = "c"; backends[2].latency_ms = 3000; backends[2].call_count = 1;
    ASSERT_EQ(selectBest(backends), 1);
}

TEST("backend_scorer: empty list returns -1") {
    std::vector<BackendScore> empty;
    ASSERT_EQ(selectBest(empty), -1);
}

TEST("backend_scorer: record updates EMA latency") {
    BackendScore b;
    b.record(100, true);
    ASSERT_EQ((int)b.latency_ms, 100); // first sample = raw
    b.record(200, true);
    // EMA: 100*0.7 + 200*0.3 = 130
    ASSERT_TRUE(b.latency_ms > 120 && b.latency_ms < 140);
}

TEST("backend_scorer: record tracks error rate") {
    BackendScore b;
    b.record(100, false); // fail
    ASSERT_TRUE(b.error_rate > 0.9); // first fail = 1.0
}
