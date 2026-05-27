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

static void test_client_connect_timeout() {
    auto c = PipeClient::connect("icmg-test-warm-nonexistent",
                                  std::chrono::milliseconds(200));
    assert(!c.has_value());
}

static void test_oversized_request_rejected() {
    PipeConfig cfg; cfg.name = "icmg-test-warm-t5"; cfg.buffer_size = 4096;
    PipeServer server(cfg);
    std::stop_source ss;
    std::thread srv([&]{
        auto conn = server.accept(ss.get_token());
        if (!conn) return;
        auto req = server.readMessage(**conn);
        server.writeMessage(**conn,
            req.empty() ? std::string(R"({"ok":false,"err":"too_large"})")
                        : std::string(R"({"ok":true})"));
        server.disconnect(**conn);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto client = PipeClient::connect(cfg.name, std::chrono::milliseconds(1000));
    assert(client.has_value());
    std::string big(8192, 'x');
    std::string resp = client->sendRequest(big);
    assert(resp.find("too_large") != std::string::npos);
    server.stop();
    ss.request_stop();
    srv.join();
}

static void test_server_stop_terminates_accept() {
    PipeConfig cfg; cfg.name = "icmg-test-warm-t5b";
    PipeServer server(cfg);
    std::stop_source ss;
    bool done = false;
    std::thread t([&]{
        auto conn = server.accept(ss.get_token());
        done = true;
        (void)conn;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.stop();
    ss.request_stop();
    t.join();
    assert(done);
}

static void test_disconnect_reuses_instance() {
    PipeConfig cfg; cfg.name = "icmg-test-warm-t5c"; cfg.max_instances = 1;
    PipeServer server(cfg);
    std::stop_source ss;
    std::thread srv([&]{
        for (int i = 0; i < 2; ++i) {
            auto conn = server.accept(ss.get_token());
            if (!conn) return;
            (void)server.readMessage(**conn);
            server.writeMessage(**conn, std::string(R"({"ok":true,"n":)") + std::to_string(i) + "}");
            server.disconnect(**conn);
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (int i = 0; i < 2; ++i) {
        auto client = PipeClient::connect(cfg.name, std::chrono::milliseconds(1000));
        assert(client.has_value());
        std::string resp = client->sendRequest(R"({"cmd":"ping"})");
        assert(resp.find("\"n\":") != std::string::npos);
    }
    server.stop();
    ss.request_stop();
    srv.join();
}

static void test_fullPipePath_format() {
    auto p = fullPipePath("foo-bar");
    assert(p == "\\\\.\\pipe\\foo-bar");
}

#ifndef ICMG_MONO_TEST
int main() {
    test_pipe_name_nonempty();
    test_server_accepts_single_client();
    test_client_ping_round_trip();
    test_client_connect_timeout();
    test_oversized_request_rejected();
    test_server_stop_terminates_accept();
    test_disconnect_reuses_instance();
    test_fullPipePath_format();
    std::cout << "test_warm_pipe: 8/8 PASS\n";
    return 0;
}

#endif  // ICMG_MONO_TEST
