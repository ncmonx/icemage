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

static void test_client_ping_round_trip() {
    PipeConfig cfg; cfg.name = "icmg-test-warm-t4";
    PipeServer server(cfg);
    std::stop_source ss;
    std::thread srv([&]{
        auto conn = server.accept(ss.get_token());
        if (!conn) return;
        auto req = server.readMessage(**conn);
        server.writeMessage(**conn, std::string("{\"ok\":true,\"echo\":\"") + req + "\"}");
        server.disconnect(**conn);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto client = PipeClient::connect(cfg.name, std::chrono::milliseconds(1000));
    assert(client.has_value());
    std::string resp = client->sendRequest(R"({"cmd":"ping"})");
    assert(resp.find("\"ok\":true") != std::string::npos);
    assert(resp.find("ping") != std::string::npos);
    server.stop();
    ss.request_stop();
    srv.join();
}

int main() {
    test_pipe_name_nonempty();
    test_server_accepts_single_client();
    test_client_ping_round_trip();
    std::cout << "test_warm_pipe: 3/3 PASS\n";
    return 0;
}
