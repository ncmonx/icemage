#include "parallel.hpp"
#include "exec_utils.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>

namespace icmg::core {

namespace {

class Semaphore {
public:
    explicit Semaphore(int count) : count_(count) {}
    void acquire() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&]{ return count_ > 0; });
        --count_;
    }
    void release() {
        {
            std::lock_guard<std::mutex> lk(m_);
            ++count_;
        }
        cv_.notify_one();
    }
private:
    std::mutex m_;
    std::condition_variable cv_;
    int count_;
};

} // namespace

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

    Semaphore slots(max_concurrency);
    std::atomic<bool> abort_flag{false};

    std::vector<std::thread> workers;
    workers.reserve(tasks.size());

    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& t = tasks[i];
        results[i].id = t.id;

        workers.emplace_back([&, i]() {
            slots.acquire();
            // fail_fast: if a sibling already failed, mark skipped and bail
            if (fail_fast && abort_flag.load()) {
                results[i].skipped = true;
                results[i].exit_code = -1;
                slots.release();
                return;
            }

            // Build argv: shell -c <command> for portable command-string exec
#ifdef _WIN32
            std::vector<std::string> argv = { "cmd.exe", "/c", tasks[i].command };
#else
            std::vector<std::string> argv = { "/bin/sh", "-c", tasks[i].command };
#endif
            ExecResult r = safeExec(argv, /*merge_stderr=*/false, tasks[i].timeout_ms);
            results[i].exit_code   = r.exit_code;
            results[i].stdout_str  = std::move(r.out);
            results[i].stderr_str  = std::move(r.err);
            results[i].duration_ms = r.duration_ms;

            if (fail_fast && r.exit_code != 0) {
                abort_flag.store(true);
            }
            slots.release();
        });
    }

    for (auto& th : workers) th.join();
    return results;
}

} // namespace icmg::core
