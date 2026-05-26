#include "llm/warm_pipe.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using namespace icmg::llm;

static void test_pipe_name_nonempty() {
    PipeConfig cfg;
    cfg.name = "icmg-test-warm";
    assert(!cfg.name.empty());
    assert(cfg.max_instances > 0);
}

#if 0  // Enabled in T4 once PipeClient exists.
static void test_server_accepts_single_client() {
    PipeConfig cfg; cfg.name = "icmg-test-warm-t3";
    PipeServer server(cfg);
    std::stop_source ss;
    bool got = false;
    std::thread acc([&]{
        auto conn = server.accept(ss.get_token());
        if (conn) got = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto client = PipeClient::connect(cfg.name, std::chrono::milliseconds(1000));
    assert(client.has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    server.stop();
    ss.request_stop();
    acc.join();
    assert(got);
}
#endif

int main() {
    test_pipe_name_nonempty();
    std::cout << "test_warm_pipe: 1/1 PASS\n";
    return 0;
}
