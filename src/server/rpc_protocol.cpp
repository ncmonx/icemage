// v1.56 T3: icmg-server RPC protocol — implementation (nlohmann/json).

#include "rpc_protocol.hpp"

#include <nlohmann/json.hpp>
#include "../core/json_safe.hpp"   // v1.68.1 safeDump

namespace icmg::server {

using nlohmann::json;

std::string serializeRequest(const RpcRequest& req) {
    json j;
    j["cmd"] = req.cmd;
    j["args"] = req.args;
    if (!req.session_id.empty()) j["session_id"] = req.session_id;
    if (!req.token.empty()) j["token"] = req.token;   // v1.68 S2
    return icmg::core::safeDump(j) + "\n";   // newline framing
}

std::string serializeResponse(const RpcResponse& res) {
    json j;
    j["ok"] = res.ok;
    if (res.ok) {
        j["out"] = res.out;
        j["exit"] = res.exit_code;
    } else {
        j["err"] = res.err;
    }
    return icmg::core::safeDump(j) + "\n";
}

std::optional<RpcRequest> parseRequest(const std::string& wire) {
    json j = json::parse(wire, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    if (!j.contains("cmd") || !j["cmd"].is_string()) return std::nullopt;

    RpcRequest req;
    req.cmd = j["cmd"].get<std::string>();
    if (j.contains("args") && j["args"].is_array()) {
        for (const auto& a : j["args"]) {
            if (a.is_string()) req.args.push_back(a.get<std::string>());
        }
    }
    if (j.contains("session_id") && j["session_id"].is_string()) {
        req.session_id = j["session_id"].get<std::string>();
    }
    if (j.contains("token") && j["token"].is_string()) {   // v1.68 S2
        req.token = j["token"].get<std::string>();
    }
    return req;
}

std::optional<RpcResponse> parseResponse(const std::string& wire) {
    json j = json::parse(wire, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;

    RpcResponse res;
    res.ok = j.value("ok", false);
    if (res.ok) {
        res.out = j.value("out", std::string{});
        res.exit_code = j.value("exit", 0);
    } else {
        res.err = j.value("err", std::string{});
    }
    return res;
}

}  // namespace icmg::server
