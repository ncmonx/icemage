// v1.56 T3: long-lived icmg-server daemon.
//
// Holds Config singleton + open DBs + Tkil session glossary in one
// resident process so per-call `icmg.exe` cold-start (~30 ms) is paid
// once. Clients connect over the "icmg-server" named pipe and send
// RpcRequest; the daemon dispatches to the command Registry, captures
// stdout, and returns an RpcResponse.
//
// Reuses icmg::llm::PipeServer for the Win named-pipe transport.

#pragma once

#include "rpc_protocol.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace icmg::server {

class IcmgServer {
public:
    explicit IcmgServer(const std::string& pipe_name = "icmg-server");
    ~IcmgServer();

    // Blocking accept loop. Returns when a "shutdown" request arrives or
    // stop() is called. Returns 0 on clean exit.
    int run();

    // Request the accept loop to exit (thread-safe).
    void stop();

    // Dispatch a single request against the command Registry, capturing
    // stdout. Exposed for unit testing without a live pipe.
    RpcResponse dispatch(const RpcRequest& req);

private:
    std::string        pipe_name_;
    std::string        token_;                // v1.68 S2: required client auth token
    std::atomic<bool>  stop_{false};
    struct GlossaryMap;                       // v1.58 FU2: per-session glossary
    std::unique_ptr<GlossaryMap> glossaries_;
};

}  // namespace icmg::server
