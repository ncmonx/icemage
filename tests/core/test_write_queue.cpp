// v1.58 F4: async write-queue tests.
//
// Queues callables, flushes on a 100ms tick / depth threshold / explicit
// flush / destruction. Used (opt-in) to batch regenerable graph writes.

#include "../test_main.hpp"
#include "../../src/core/write_queue.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using namespace icmg::core;

TEST("write_queue: flush runs all queued jobs") {
    std::atomic<int> n{0};
    {
        WriteQueue q;
        for (int i = 0; i < 10; ++i) q.enqueue([&]{ ++n; });
        q.flush();
        ASSERT_EQ(n.load(), 10);
    }
}

TEST("write_queue: destruction flushes outstanding jobs") {
    std::atomic<int> n{0};
    {
        WriteQueue q;
        q.enqueue([&]{ ++n; });
        q.enqueue([&]{ ++n; });
        // no explicit flush — dtor must drain.
    }
    ASSERT_EQ(n.load(), 2);
}

TEST("write_queue: depth threshold triggers auto-flush") {
    std::atomic<int> n{0};
    WriteQueue q;
    q.setDepthThreshold(5);
    for (int i = 0; i < 5; ++i) q.enqueue([&]{ ++n; });
    // Give the background flusher a moment.
    for (int spin = 0; spin < 200 && n.load() < 5; ++spin)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_EQ(n.load(), 5);
}

TEST("write_queue: empty flush is a no-op") {
    WriteQueue q;
    q.flush();   // must not hang or throw
    ASSERT_TRUE(true);
}

TEST("write_queue: jobs run in FIFO order") {
    std::vector<int> order;
    std::mutex m;
    {
        WriteQueue q;
        for (int i = 0; i < 5; ++i)
            q.enqueue([&, i]{ std::lock_guard<std::mutex> g(m); order.push_back(i); });
        q.flush();
    }
    ASSERT_EQ(order.size(), 5u);
    for (int i = 0; i < 5; ++i) ASSERT_EQ(order[i], i);
}
