// v1.58 F4: async batch write-queue.
//
// Queues write jobs (callables) and drains them on a background thread —
// every flush_interval_ms tick, when the queue depth crosses a threshold,
// on an explicit flush(), or at destruction. Lets graph updates return
// without blocking on each INSERT.
//
// DURABILITY: only use for REGENERABLE data (graph_nodes/edges — re-derived
// by `icmg graph update`). A crash before the next flush loses queued jobs;
// that is acceptable for derived data but NOT for memory/decisions, which
// must stay synchronous.

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace icmg::core {

class WriteQueue {
public:
    using Job = std::function<void()>;

    explicit WriteQueue(int flush_interval_ms = 100);
    ~WriteQueue();                       // drains then joins

    WriteQueue(const WriteQueue&) = delete;
    WriteQueue& operator=(const WriteQueue&) = delete;

    // Queue a job. Thread-safe. Wakes the flusher when depth threshold hit.
    void enqueue(Job job);

    // Run all currently-queued jobs synchronously (on the caller thread is
    // avoided — signals the worker and waits until the queue is empty).
    void flush();

    // Auto-flush when queued depth reaches n (default 256).
    void setDepthThreshold(std::size_t n);

    std::size_t pending() const;

private:
    void workerLoop();
    void drainLocked(std::unique_lock<std::mutex>& lk);

    mutable std::mutex      mtx_;
    std::condition_variable cv_;
    std::condition_variable drained_cv_;
    std::deque<Job>         q_;
    std::size_t             depth_threshold_ = 256;
    int                     interval_ms_ = 100;
    bool                    stop_ = false;
    bool                    draining_ = false;
    std::thread             worker_;
};

}  // namespace icmg::core
