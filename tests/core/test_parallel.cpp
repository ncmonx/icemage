// tests/core/test_parallel.cpp
// Phase 21 task 5b — subprocess fan-out primitive.

#include "../test_main.hpp"
#include "../../src/core/parallel.hpp"
#include <chrono>

using namespace icmg::core;

TEST("parallel: empty input returns empty") {
    auto r = parallel({}, 4, false);
    ASSERT_EQ((int)r.size(), 0);
}

TEST("parallel: single task succeeds") {
    std::vector<ParallelTask> ts;
    ParallelTask t; t.command = "echo hello"; t.id = "x";
    ts.push_back(t);
    auto r = parallel(ts, 1, false);
    ASSERT_EQ((int)r.size(), 1);
    ASSERT_EQ(r[0].exit_code, 0);
    ASSERT_TRUE(r[0].stdout_str.find("hello") != std::string::npos);
    ASSERT_EQ(r[0].id, std::string("x"));
}

TEST("parallel: 4 tasks finish faster than serial sleep sum") {
    // Each task sleeps 500ms; parallel of 4 should finish in ~500ms, not 2000ms.
    std::vector<ParallelTask> ts;
    for (int i = 0; i < 4; ++i) {
        ParallelTask t;
#ifdef _WIN32
        t.command = "ping -n 1 127.0.0.1 >NUL";  // ~ms-scale wait
#else
        t.command = "sleep 0.5";
#endif
        t.id = "t" + std::to_string(i);
        ts.push_back(t);
    }
    auto t0 = std::chrono::steady_clock::now();
    auto r = parallel(ts, 4, false);
    auto t1 = std::chrono::steady_clock::now();
    int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    ASSERT_EQ((int)r.size(), 4);
    for (auto& res : r) ASSERT_EQ(res.exit_code, 0);

    // Should take roughly the longest task, not sum. Generous bound for CI variance.
#ifndef _WIN32
    ASSERT_TRUE(elapsed < 1500);  // 4 sequential = 2000ms; parallel < 1500
#endif
}

TEST("parallel: results in submission order") {
    std::vector<ParallelTask> ts;
    for (int i = 0; i < 3; ++i) {
        ParallelTask t;
        t.command = "echo " + std::to_string(i);
        t.id = "t" + std::to_string(i);
        ts.push_back(t);
    }
    auto r = parallel(ts, 4, false);
    ASSERT_EQ((int)r.size(), 3);
    ASSERT_EQ(r[0].id, std::string("t0"));
    ASSERT_EQ(r[1].id, std::string("t1"));
    ASSERT_EQ(r[2].id, std::string("t2"));
}

TEST("parallel: max_concurrency=0 picks reasonable default") {
    std::vector<ParallelTask> ts;
    ParallelTask t; t.command = "echo ok";
    ts.push_back(t);
    auto r = parallel(ts, 0, false);  // 0 → use hardware_concurrency
    ASSERT_EQ((int)r.size(), 1);
    ASSERT_EQ(r[0].exit_code, 0);
}

TEST("parallel: non-zero exit propagates per-task") {
    std::vector<ParallelTask> ts;
    ParallelTask t1; t1.command = "true";  ts.push_back(t1);
#ifdef _WIN32
    ParallelTask t2; t2.command = "exit 7";   ts.push_back(t2);
#else
    ParallelTask t2; t2.command = "exit 7";   ts.push_back(t2);
#endif
    auto r = parallel(ts, 2, false);
    ASSERT_EQ((int)r.size(), 2);
    ASSERT_EQ(r[0].exit_code, 0);
    ASSERT_TRUE(r[1].exit_code != 0);
}

int main() {
    std::cout << "=== parallel tests ===\n";
    return icmg::test::run_all();
}
