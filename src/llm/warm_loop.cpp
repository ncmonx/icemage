#include "warm_loop.hpp"
#include "warm_pool.hpp"
#include "llama_runner.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace icmg::llm {

namespace {

struct DaemonState {
    std::atomic<int> served{0};
    std::atomic<int> active_clients{0};
    std::chrono::steady_clock::time_point started_at;
    std::atomic<bool> stop_flag{false};
};

struct ClientCounter {
    DaemonState& ds_;
    explicit ClientCounter(DaemonState& ds) : ds_(ds) { ds_.active_clients++; }
    ~ClientCounter() { ds_.active_clients--; }
    ClientCounter(const ClientCounter&) = delete;
    ClientCounter& operator=(const ClientCounter&) = delete;
};

std::string extractString(const std::string& json, const std::string& key) {
    std::regex r("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    std::smatch m;
    if (std::regex_search(json, m, r)) return m[1].str();
    return {};
}
int extractInt(const std::string& json, const std::string& key, int def) {
    std::regex r("\"" + key + "\"\\s*:\\s*(-?\\d+)");
    std::smatch m;
    if (std::regex_search(json, m, r)) return std::stoi(m[1].str());
    return def;
}
double extractDouble(const std::string& json, const std::string& key, double def) {
    std::regex r("\"" + key + "\"\\s*:\\s*(-?\\d+(?:\\.\\d+)?)");
    std::smatch m;
    if (std::regex_search(json, m, r)) return std::stod(m[1].str());
    return def;
}

std::string escapeJson(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    o += buf;
                } else o += c;
        }
    }
    return o;
}

std::string handleRequest(const std::string& req,
                           DaemonState& ds,
                           LlamaRunner& runner,
                           const std::string& model_id)
{
    std::string id = extractString(req, "id");
    std::string cmd = extractString(req, "cmd");

    auto reply_err = [&](const std::string& msg, const std::string& code) {
        return std::string{"{\"id\":\""} + escapeJson(id) +
               "\",\"ok\":false,\"err\":\"" + escapeJson(msg) +
               "\",\"err_code\":\"" + code + "\"}";
    };

    if (cmd == "ping") {
        return "{\"id\":\"" + escapeJson(id) + "\",\"ok\":true}";
    }
    if (cmd == "status") {
        auto up = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - ds.started_at).count();
        return "{\"id\":\"" + escapeJson(id) +
               "\",\"ok\":true,\"model\":\"" + escapeJson(model_id) +
               "\",\"uptime_s\":" + std::to_string(up) +
               ",\"served\":" + std::to_string(ds.served.load()) +
               ",\"active_clients\":" + std::to_string(ds.active_clients.load()) + "}";
    }
    if (cmd == "shutdown") {
        ds.stop_flag = true;
        return "{\"id\":\"" + escapeJson(id) + "\",\"ok\":true,\"shutting_down\":true}";
    }
    if (cmd == "infer") {
        std::string prompt = extractString(req, "prompt");
        if (prompt.empty()) return reply_err("empty prompt", "bad_request");
        InferParams ip;
        ip.max_tokens     = extractInt(req, "max_tokens", 4096);
        ip.temperature    = (float)extractDouble(req, "temperature", 0.7);
        ip.top_p          = (float)extractDouble(req, "top_p", 0.95);
        ip.repeat_penalty = (float)extractDouble(req, "repeat_penalty", 1.15);
        std::string stop_str = extractString(req, "stop");
        if (!stop_str.empty()) ip.stop = stop_str;

        auto t0 = std::chrono::steady_clock::now();
        auto res = runner.infer(prompt, ip);
        auto t1 = std::chrono::steady_clock::now();
        int wall = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();

        if (!res.ok) return reply_err(res.error, "infer_fail");
        ds.served++;
        return "{\"id\":\"" + escapeJson(id) +
               "\",\"ok\":true,\"text\":\"" + escapeJson(res.text) +
               "\",\"tok_in\":" + std::to_string(res.tokens_in) +
               ",\"tok_out\":" + std::to_string(res.tokens_out) +
               ",\"wall_ms\":" + std::to_string(wall) + "}";
    }
    return reply_err("unknown cmd: " + cmd, "bad_request");
}

void writePidFile(const std::string& path) {
    if (path.empty()) return;
    std::ofstream f(path);
    if (!f) return;
#ifdef _WIN32
    f << GetCurrentProcessId();
#else
    f << (int)getpid();
#endif
}

void unlinkPid(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

}  // anon ns

int runWarmLoop(const WarmLoopConfig& cfg) {
    DaemonState ds;
    ds.started_at = std::chrono::steady_clock::now();

    writePidFile(cfg.pid_file_path);

    std::string err;
    auto* runner = WarmPool::instance().acquire(err);
    if (!runner) {
        std::cerr << "warm-loop: model load failed: " << err << "\n";
        unlinkPid(cfg.pid_file_path);
        return 2;
    }
    std::string model_id = WarmPool::instance().activeModelId();

    PipeServer server(cfg.pipe);
    std::atomic<bool> ss_stop{false};

    std::vector<std::thread> workers;
    workers.reserve(cfg.worker_threads);
    for (int i = 0; i < cfg.worker_threads; ++i) {
        workers.emplace_back([&]{
            while (!ds.stop_flag && !ss_stop.load()) {
                auto conn = server.accept(ss_stop);
                if (!conn) break;
                {
                    ClientCounter cc(ds);
                    auto req = server.readMessage(**conn);
                    if (!req.empty()) {
                        auto resp = handleRequest(req, ds, *runner, model_id);
                        server.writeMessage(**conn, resp);
                    }
                }  // cc destructor decrements
                server.disconnect(**conn);
            }
        });
    }

    while (!ds.stop_flag) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server.stop();
    ss_stop.store(true);
    for (auto& t : workers) if (t.joinable()) t.join();
    unlinkPid(cfg.pid_file_path);
    return 0;
}

}  // namespace icmg::llm
