// v1.56 T3: `icmg server` — control the long-lived icmg-server daemon.
//
//   icmg server start      Run the daemon (blocking; usually spawned detached)
//   icmg server stop       Send shutdown request to a running daemon
//   icmg server status     Ping the daemon, report up/down
//   icmg server exec <...>  Round-trip one command through the daemon (test)

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../server/icmg_server.hpp"
#include "../../server/rpc_protocol.hpp"
#include "../../core/server_token.hpp"        // v1.68 S2: client auth token
#include "../../llm/warm_pipe.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

constexpr const char* kPipeName = "icmg-server";

// Send one request, return response (nullopt if no daemon).
std::optional<icmg::server::RpcResponse>
roundTrip(const icmg::server::RpcRequest& req,
          std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    auto client = icmg::llm::PipeClient::connect(kPipeName, timeout);
    if (!client.has_value()) return std::nullopt;
    // v1.68 S2: attach this user's token so the daemon authorizes the request.
    icmg::server::RpcRequest authed = req;
    authed.token = icmg::core::readServerToken();
    std::string wire = icmg::server::serializeRequest(authed);
    std::string resp = client->sendRequest(wire);
    if (resp.empty()) return std::nullopt;
    return icmg::server::parseResponse(resp);
}

}  // namespace

class ServerCommand : public BaseCommand {
public:
    std::string name()        const override { return "server"; }
    std::string description() const override {
        return "Long-lived icmg daemon (start/stop/status/exec)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg server <action>\n\n"
            "Actions:\n"
            "  start          Run the daemon (blocking — spawn detached for bg)\n"
            "  stop           Send shutdown to a running daemon\n"
            "  status         Ping the daemon; report up/down\n"
            "  exec <cmd...>  Round-trip one command through the daemon\n\n"
            "The daemon holds Config + DBs + Tkil session glossary resident so\n"
            "per-call cold-start (~30 ms) is paid once. Pipe: \\\\.\\pipe\\icmg-server\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& action = args[0];

        if (action == "start") {
            // v1.57 S1: single-instance guard via pipe-ping. If a daemon is
            // already answering on the pipe, refuse to start a second one
            // (two servers on the same pipe name race + drop connections).
            {
                icmg::server::RpcRequest ping;
                ping.cmd = "ping";
                auto up = roundTrip(ping, std::chrono::milliseconds(500));
                if (up.has_value() && up->ok) {
                    std::cerr << "icmg server: already running — refusing "
                                 "second instance (use `icmg server stop` first)\n";
                    return 1;
                }
            }
            std::cerr << "icmg server: starting (pipe \\\\.\\pipe\\" << kPipeName
                      << ") — Ctrl-C or `icmg server stop` to exit\n";
            icmg::server::IcmgServer srv(kPipeName);
            return srv.run();
        }

        if (action == "stop") {
            icmg::server::RpcRequest req;
            req.cmd = "shutdown";
            auto res = roundTrip(req);
            if (!res.has_value()) {
                std::cout << "icmg server: not running (no pipe)\n";
                return 1;
            }
            std::cout << "icmg server: stop sent\n";
            return 0;
        }

        if (action == "status") {
            icmg::server::RpcRequest req;
            req.cmd = "ping";
            auto res = roundTrip(req);
            if (res.has_value() && res->ok) {
                std::cout << "icmg server: UP\n";
                return 0;
            }
            std::cout << "icmg server: DOWN\n";
            return 1;
        }

        if (action == "exec") {
            if (args.size() < 2) {
                std::cerr << "icmg server exec: need a command\n";
                return 2;
            }
            icmg::server::RpcRequest req;
            req.cmd = args[1];
            req.args.assign(args.begin() + 2, args.end());
            if (const char* sid = std::getenv("CLAUDE_SESSION_ID"); sid && *sid)
                req.session_id = sid;
            auto res = roundTrip(req, std::chrono::milliseconds(60000));
            if (!res.has_value()) {
                std::cerr << "icmg server: not running — start with "
                             "`icmg server start`\n";
                return 1;
            }
            if (!res->ok) {
                std::cerr << "icmg server exec error: " << res->err << "\n";
                return 1;
            }
            std::cout << res->out;
            return res->exit_code;
        }

        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("server", ServerCommand);

}  // namespace icmg::cli
