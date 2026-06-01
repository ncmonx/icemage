// v1.58 F4: async batch write-queue — implementation.

#include "write_queue.hpp"

#include <chrono>

namespace icmg::core {

WriteQueue::WriteQueue(int flush_interval_ms)
    : interval_ms_(flush_interval_ms) {
    worker_ = std::thread([this] { workerLoop(); });
}

WriteQueue::~WriteQueue() {
    {
        std::lock_guard<std::mutex> g(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    // Drain anything left synchronously (worker already ran final batch,
    // but guard against a job enqueued during teardown).
    std::unique_lock<std::mutex> lk(mtx_);
    drainLocked(lk);
}

void WriteQueue::setDepthThreshold(std::size_t n) {
    std::lock_guard<std::mutex> g(mtx_);
    depth_threshold_ = n ? n : 1;
}

std::size_t WriteQueue::pending() const {
    std::lock_guard<std::mutex> g(mtx_);
    return q_.size();
}

void WriteQueue::enqueue(Job job) {
    bool wake = false;
    {
        std::lock_guard<std::mutex> g(mtx_);
        q_.push_back(std::move(job));
        if (q_.size() >= depth_threshold_) wake = true;
    }
    if (wake) cv_.notify_one();
}

// Run every queued job, releasing the lock around each call so enqueue()
// stays responsive. Re-checks the queue until empty.
void WriteQueue::drainLocked(std::unique_lock<std::mutex>& lk) {
    draining_ = true;
    while (!q_.empty()) {
        Job job = std::move(q_.front());
        q_.pop_front();
        lk.unlock();
        try { job(); } catch (...) { /* a bad job must not kill the queue */ }
        lk.lock();
    }
    draining_ = false;
    drained_cv_.notify_all();
}

void WriteQueue::flush() {
    std::unique_lock<std::mutex> lk(mtx_);
    drainLocked(lk);
}

void WriteQueue::workerLoop() {
    std::unique_lock<std::mutex> lk(mtx_);
    while (!stop_) {
        // Wait for the interval, an enqueue past threshold, or stop.
        cv_.wait_for(lk, std::chrono::milliseconds(interval_ms_),
                     [this] { return stop_ || !q_.empty(); });
        if (!q_.empty()) drainLocked(lk);
    }
    // Final drain on stop.
    drainLocked(lk);
}

}  // namespace icmg::core
