// v1.78.2 Phase B: writeThrough sink hook on RecallCache::put.
//
// Pattern: RecallCache exposes setPersistSink(callback). Tests pass a mock
// sink that records calls. Production wires sink to a lambda that enqueues
// the row write on the daemon's WriteQueue.

#include "../test_main.hpp"
#include "../../src/core/recall_cache.hpp"
#include "../../src/core/recall_cache_persist.hpp"

#include <atomic>
#include <vector>
#include <string>

using icmg::core::RecallCache;

TEST("persist-sink: put fires sink when set") {
    RecallCache c;
    c.setCap(16, 1u << 20);

    std::atomic<int> calls{0};
    std::string last_key, last_value;
    c.setPersistSink([&](const std::string& k, const std::string& v, std::size_t bytes) {
        ++calls;
        last_key = k;
        last_value = v;
        (void)bytes;
    });

    c.putAt("query1", "result-payload", 1000);
    ASSERT_EQ(calls.load(), 1);
    ASSERT_EQ(last_key, std::string("query1"));
    ASSERT_EQ(last_value, std::string("result-payload"));
}

TEST("persist-sink: not called when sink unset (back-compat)") {
    RecallCache c;
    c.setCap(16, 1u << 20);
    // No setPersistSink call — must not crash on put.
    c.putAt("query2", "payload", 1000);
    auto v = c.getAt("query2", 1000);
    ASSERT_TRUE(v.has_value());
}

TEST("persist-sink: byte_size matches Entry.bytes (key+value)") {
    RecallCache c;
    c.setCap(16, 1u << 20);

    std::size_t sink_bytes = 0;
    c.setPersistSink([&](const std::string& k, const std::string& v, std::size_t bytes) {
        (void)k; (void)v;
        sink_bytes = bytes;
    });

    c.putAt("k", "vvvvvvvv", 1000);
    ASSERT_EQ(sink_bytes, std::string("k").size() + std::string("vvvvvvvv").size());
}

TEST("persist-sink: every put fires (no dedup at sink layer)") {
    RecallCache c;
    c.setCap(16, 1u << 20);

    std::atomic<int> calls{0};
    c.setPersistSink([&](const std::string&, const std::string&, std::size_t){ ++calls; });

    c.putAt("k", "v1", 1000);
    c.putAt("k", "v2", 1001);   // replace same key → still emits sink (UPSERT semantics)
    c.putAt("k2", "v3", 1002);

    ASSERT_EQ(calls.load(), 3);
}

TEST("persist-sink: clearing sink stops calls") {
    RecallCache c;
    c.setCap(16, 1u << 20);

    std::atomic<int> calls{0};
    c.setPersistSink([&](const std::string&, const std::string&, std::size_t){ ++calls; });
    c.putAt("a", "1", 1000);

    c.setPersistSink({});   // clear
    c.putAt("b", "2", 1001);

    ASSERT_EQ(calls.load(), 1);
}
