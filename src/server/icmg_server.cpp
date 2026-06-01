// v1.56 T3: long-lived icmg-server daemon — implementation.

#include "icmg_server.hpp"
#include <atomic>
#include "../cli/base_command.hpp"
#include "../core/registry.hpp"
#include "../core/server_token.hpp"           // v1.68 S2: IPC auth
#include "../llm/warm_pipe.hpp"
#include "../tkil/session_glossary.hpp"   // v1.58 FU2: cross-call glossary

#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace icmg::server {

namespace {

// RAII: temporarily redirect std::cout (and std::cerr) into a stringstream
// so we can capture a command's output. Restores original buffers on
// destruction even if run() throws.
struct CoutCapture {
    std::ostringstream buf;
    std::streambuf* old_cout;
    std::streambuf* old_cerr;
    CoutCapture()
        : old_cout(std::cout.rdbuf(buf.rdbuf())),
          old_cerr(std::cerr.rdbuf(buf.rdbuf())) {}
    ~CoutCapture() {
        std::cout.rdbuf(old_cout);
        std::cerr.rdbuf(old_cerr);
    }
};

}  // namespace

// v1.58 FU2: per-session Tkil glossary. Persisting one glossary per
// session_id across dispatch calls lets recurring phrases collapse to
// tokens that stay stable for the life of a daemon session (the in-process
// process-glossary used by `icmg run` is lost when each short-lived icmg
// process exits — the daemon keeps it warm).
struct IcmgServer::GlossaryMap {
    std::mutex mtx;
    std::unordered_map<std::string, icmg::tkil::SessionGlossary> by_session;
};

IcmgServer::IcmgServer(const std::string& pipe_name)
    : pipe_name_(pipe_name),
      glossaries_(std::make_unique<GlossaryMap>()) {}
IcmgServer::~IcmgServer() = default;

void IcmgServer::stop() { stop_ = true; }

RpcResponse IcmgServer::dispatch(const RpcRequest& req) {
    RpcResponse res;

    // Built-in control verbs.
    if (req.cmd == "shutdown") {
        stop_ = true;
        res.ok = true;
        res.out = "shutting down\n";
        return res;
    }
    if (req.cmd == "ping") {
        res.ok = true;
        res.out = "pong\n";
        return res;
    }

    auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();

    // Resolve handler: direct cmd, else compound "cmd-arg0".
    std::unique_ptr<icmg::cli::BaseCommand> handler;
    std::vector<std::string> rest = req.args;
    if (reg.has(req.cmd)) {
        handler = reg.create(req.cmd);
    } else if (!req.args.empty()) {
        std::string compound = req.cmd + "-" + req.args[0];
        if (reg.has(compound)) {
            handler = reg.create(compound);
            rest.assign(req.args.begin() + 1, req.args.end());
        }
    }

    if (!handler) {
        res.ok = false;
        res.err = "unknown command '" + req.cmd + "'";
        return res;
    }

    int rc = 0;
    {
        CoutCapture cap;
        try {
            rc = handler->run(rest);
        } catch (const std::exception& e) {
            // Restore buffers (via cap dtor) before building error.
            std::string captured = cap.buf.str();
            res.ok = false;
            res.err = std::string("command threw: ") + e.what();
            res.out = captured;
            res.exit_code = 1;
            return res;
        }
        res.out = cap.buf.str();
    }
    res.ok = true;
    res.exit_code = rc;

    // v1.58 FU2: apply the session's persistent glossary to the captured
    // output. Recurring lines across calls in the same session collapse to
    // stable $S<N> tokens (expandable via `icmg expand`). Keyed by
    // session_id; sessions without an id share the "" bucket.
    if (glossaries_) {
        std::lock_guard<std::mutex> g(glossaries_->mtx);
        auto& gl = glossaries_->by_session[req.session_id];
        res.out = gl.apply(res.out);
    }
    return res;
}

int IcmgServer::run() {
    // v1.68 S2: establish the per-user auth token. Every client request must
    // present this exact token or the worker refuses to dispatch (incl.
    // shutdown/ping), so a foreign pipe client cannot drive the daemon.
    token_ = icmg::core::loadOrCreateServerToken();

    icmg::llm::PipeConfig cfg;
    cfg.name = pipe_name_;
    cfg.max_instances = 4;

    icmg::llm::PipeServer server(cfg);
    std::atomic<bool> ss_stop{false};

    // Multi-worker accept loop — mirrors the proven v1.52 warm-loop daemon
    // pattern. A single-threaded accept/disconnect loop stalled after the
    // first request because each pipe instance must be re-armed while other
    // instances stay listening; N workers keep the pool warm.
    const int kWorkers = cfg.max_instances;
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&] {
            while (!stop_ && !ss_stop.load()) {
                auto conn = server.accept(ss_stop);
                if (!conn) break;                       // stopped
                std::string wire = server.readMessage(**conn);
                if (!wire.empty()) {
                    auto req = parseRequest(wire);
                    RpcResponse res;
                    if (!req.has_value()) {
                        res.ok = false;
                        res.err = "malformed request";
                    } else if (!icmg::core::tokenMatches(token_, req->token)) {
                        // v1.68 S2: reject before dispatch — gates shutdown/ping too.
                        res.ok = false;
                        res.err = "unauthorized";
                        res.exit_code = 13;
                    } else {
                        res = dispatch(*req);
                    }
                    server.writeMessage(**conn, serializeResponse(res));
                }
                server.disconnect(**conn);
            }
        });
    }

    // Main thread idles until a worker sets stop_ (via a "shutdown" request)
    // or stop() is called externally.
    while (!stop_) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server.stop();
    ss_stop.store(true);
    for (auto& t : workers) if (t.joinable()) t.join();
    return 0;
}

}  // namespace icmg::server
