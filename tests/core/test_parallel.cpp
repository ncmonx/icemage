// tests/core/test_parallel.cpp
// Phase 21 task 5b — subprocess fan-out primitive.

#include "../test_main.hpp"
#include "../../src/core/parallel.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>

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
        t.command = "ping -n 1 127.0.0.1";  // ~ms wait; no >NUL — the exec fast-path splits on '>' (flaky exit)
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
#ifdef _WIN32
    ParallelTask t1; t1.command = "exit 0"; ts.push_back(t1);
    ParallelTask t2; t2.command = "exit 7"; ts.push_back(t2);
#else
    ParallelTask t1; t1.command = "true";   ts.push_back(t1);
    ParallelTask t2; t2.command = "exit 7"; ts.push_back(t2);
#endif
    auto r = parallel(ts, 2, false);
    ASSERT_EQ((int)r.size(), 2);
    ASSERT_EQ(r[0].exit_code, 0);
    ASSERT_TRUE(r[1].exit_code != 0);
}

// Phase 24: fail_fast cancellation tests.
TEST("parallel: fail_fast skips later tasks when earlier fails") {
    // First task fails fast; subsequent tasks (queued behind cap) should be
    // marked skipped rather than executed.
    std::vector<ParallelTask> ts;
    ParallelTask t0; t0.command = "exit 1"; t0.id = "fail0"; ts.push_back(t0);
    for (int i = 0; i < 4; ++i) {
        ParallelTask t;
#ifdef _WIN32
        t.command = "ping -n 2 127.0.0.1 >NUL";  // ~1s, gives fail0 time to abort siblings
#else
        t.command = "sleep 1";
#endif
        t.id = "t" + std::to_string(i);
        ts.push_back(t);
    }
    auto r = parallel(ts, /*max_concurrency=*/1, /*fail_fast=*/true);
    ASSERT_EQ((int)r.size(), (int)ts.size());
    ASSERT_TRUE(r[0].exit_code != 0);   // failure
    // Tasks start in submission order, so with cap=1 fail0 always runs first
    // and every later task must be skipped -- no race allowed.
    ASSERT_FALSE(r[0].skipped);
    for (size_t i = 1; i < r.size(); ++i) ASSERT_TRUE(r[i].skipped);
}

TEST("parallel: cap=1 starts tasks in submission order") {
    // Each task appends its id to one file; with cap=1 the file must read
    // "0 1 2 3 4 5 6 7 " in order. Thread-per-task scheduling broke this.
    const std::string path = "icmg_test_parallel_order.txt";
    std::remove(path.c_str());
    std::vector<ParallelTask> ts;
    for (int i = 0; i < 8; ++i) {
        ParallelTask t;
        t.command = "echo " + std::to_string(i) + " >> " + path;
        ts.push_back(t);
    }
    auto r = parallel(ts, /*max_concurrency=*/1, /*fail_fast=*/false);
    ASSERT_EQ((int)r.size(), 8);
    std::ifstream in(path);
    std::string got, tok;
    while (in >> tok) got += tok + " ";
    in.close();
    std::remove(path.c_str());
    ASSERT_EQ(got, std::string("0 1 2 3 4 5 6 7 "));
}

TEST("parallel: fail_fast=false runs all tasks despite failures") {
    std::vector<ParallelTask> ts;
    ParallelTask t0; t0.command = "exit 1"; t0.id = "fail"; ts.push_back(t0);
    ParallelTask t1; t1.command = "echo ok"; t1.id = "ok";  ts.push_back(t1);

    auto r = parallel(ts, 2, /*fail_fast=*/false);
    ASSERT_EQ((int)r.size(), 2);
    ASSERT_TRUE(r[0].exit_code != 0);
    ASSERT_EQ(r[1].exit_code, 0);
    ASSERT_FALSE(r[0].skipped);
    ASSERT_FALSE(r[1].skipped);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
