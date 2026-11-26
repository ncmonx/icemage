#include "parallel.hpp"
#include "exec_utils.hpp"
#include <thread>
#include <atomic>
#include <algorithm>

namespace icmg::core {

std::vector<ParallelResult>
parallel(const std::vector<ParallelTask>& tasks,
         int max_concurrency,
         bool fail_fast) {

    std::vector<ParallelResult> results(tasks.size());
    if (tasks.empty()) return results;

    if (max_concurrency <= 0) {
        unsigned hw = std::thread::hardware_concurrency();
        max_concurrency = (hw > 0) ? (int)hw : 4;
    }
    if (max_concurrency > 32) max_concurrency = 32;
    if ((int)tasks.size() < max_concurrency) max_concurrency = (int)tasks.size();

    for (size_t i = 0; i < tasks.size(); ++i) results[i].id = tasks[i].id;

    // Fixed pool of max_concurrency workers pulling the next index in
    // submission order. (A thread per task racing on a semaphore let the OS
    // scheduler pick the start order, so fail_fast could run later tasks
    // before the failing earlier one -- flaky test_parallel fail_fast.)
    std::atomic<size_t> next{0};
    std::atomic<bool> abort_flag{false};

    auto worker = [&]() {
        for (size_t i = next.fetch_add(1); i < tasks.size(); i = next.fetch_add(1)) {
            // fail_fast: if a sibling already failed, mark skipped
            if (fail_fast && abort_flag.load()) {
                results[i].skipped = true;
                results[i].exit_code = -1;
                continue;
            }

            // Use shell-pass-through so paths-with-spaces and quotes survive.
            // safeExecShell uses `cmd.exe /s /c "<command>"` on Windows
            // (preserving internal quotes) and `/bin/sh -c <command>` on POSIX.
            ExecResult r = safeExecShell(tasks[i].command, /*merge_stderr=*/false,
                                          tasks[i].timeout_ms);
            results[i].exit_code   = r.exit_code;
            results[i].stdout_str  = std::move(r.out);
            results[i].stderr_str  = std::move(r.err);
            results[i].duration_ms = r.duration_ms;

            if (fail_fast && r.exit_code != 0) {
                abort_flag.store(true);
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve((size_t)max_concurrency);
    for (int w = 0; w < max_concurrency; ++w) workers.emplace_back(worker);

    for (auto& th : workers) th.join();
    return results;
}

} // namespace icmg::core
