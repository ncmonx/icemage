#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::core {

// Phase 21 Task 5b: subprocess fan-out primitive.
//
// Each task runs as a child process via core::safeExec on a dedicated thread.
// Concurrency is capped by max_concurrency (semaphore-style); tasks beyond
// the cap queue and start as slots free up. fail_fast cancels pending tasks
// (does NOT kill in-flight ones) when the first non-zero exit is observed.

struct ParallelTask {
    std::string command;       // shell-style; safe-exec'd via /bin/sh -c (Unix) or cmd /c (Win)
    int         timeout_ms = 60000;
    std::string id;            // optional tag, echoed back in result
};

struct ParallelResult {
    int         exit_code   = 0;
    std::string stdout_str;
    std::string stderr_str;
    int         duration_ms = 0;
    std::string id;
    bool        skipped     = false;  // true if fail_fast cancelled this task before start
};

// Run all tasks; returns results in submission order.
// max_concurrency: 0 or negative → use std::thread::hardware_concurrency() (capped at 32).
std::vector<ParallelResult>
parallel(const std::vector<ParallelTask>& tasks,
         int max_concurrency = 0,
         bool fail_fast = false);

} // namespace icmg::core
