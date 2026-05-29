// v1.56 T3: icmg-server RPC protocol.
//
// Newline-delimited JSON over a Win named pipe. One request -> one
// response per round-trip. Used by the long-lived icmg-server daemon
// (src/server/icmg_server.cpp) and any client (dispatcher fast-path or
// `icmg server --exec`).

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace icmg::server {

struct RpcRequest {
    std::string              cmd;          // first argv token (command name)
    std::vector<std::string> args;         // remaining argv tokens
    std::string              session_id;   // hook session id (for glossary)
    std::string              token;        // v1.68 S2: per-user auth token
};

struct RpcResponse {
    bool        ok = false;
    std::string out;          // captured stdout (filtered)
    std::string err;          // error message when !ok
    int         exit_code = 0;
};

// Serialize to a single newline-terminated JSON line.
std::string serializeRequest(const RpcRequest& req);
std::string serializeResponse(const RpcResponse& res);

// Parse a wire message. Returns nullopt on malformed JSON or missing
// required fields (request requires "cmd").
std::optional<RpcRequest>  parseRequest(const std::string& wire);
std::optional<RpcResponse> parseResponse(const std::string& wire);

}  // namespace icmg::server
