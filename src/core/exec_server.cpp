// v1.13.0: exec IPC server.
//
// Per-CLI invocation routing — client connects, sends argv+cwd+env+stdin,
// server dispatches via Registry<BaseCommand>, streams stdout/stderr back
// as framed JSON, ends with terminal frame containing exit code.
//
// Concurrency: one accept thread + one worker thread per request.
// Bounded worker pool (max 32 concurrent) to prevent thread bomb.
//
// Cwd handling: SetCurrentDirectory is process-global. Workers serialize
// chdir + exec block via a single mutex. Acceptable for typical CLI
// invocation rates (<10/sec).

#include "exec_server.hpp"
#include "exec_protocol.hpp"
#include "registry.hpp"
#include "config.hpp"
#include "../cli/base_command.hpp"

#include <nlohmann/json.hpp>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#  include <errno.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::core::exec_server {

namespace {

// Limit concurrent workers.
constexpr int kMaxWorkers = 32;
std::atomic<int> g_active_workers{0};

// Serialize SetCurrentDirectory + cmd exec. See header comment.
std::mutex g_chdir_mu;

// Framing helpers ---------------------------------------------------------

#ifdef _WIN32
bool readFrame(HANDLE h, std::string& out) {
    char len_buf[4];
    DWORD got = 0;
    if (!ReadFile(h, len_buf, 4, &got, nullptr) || got != 4) return false;
    uint32_t n = exec_proto::parseLength(len_buf, 4);
    if (n == 0 || n > 16 * 1024 * 1024) return false;  // 16MB cap
    out.resize(n);
    size_t total = 0;
    while (total < n) {
        DWORD chunk = 0;
        if (!ReadFile(h, out.data() + total, (DWORD)(n - total), &chunk, nullptr)
            || chunk == 0) return false;
        total += chunk;
    }
    return true;
}

bool writeFrame(HANDLE h, const std::string& json_payload) {
    std::string framed = exec_proto::frame(json_payload);
    DWORD written = 0;
    return WriteFile(h, framed.data(), (DWORD)framed.size(), &written, nullptr)
        && written == framed.size();
}
#else
bool readFrame(int fd, std::string& out) {
    char len_buf[4];
    ssize_t got = ::recv(fd, len_buf, 4, MSG_WAITALL);
    if (got != 4) return false;
    uint32_t n = exec_proto::parseLength(len_buf, 4);
    if (n == 0 || n > 16 * 1024 * 1024) return false;
    out.resize(n);
    size_t total = 0;
    while (total < n) {
        ssize_t r = ::recv(fd, out.data() + total, n - total, MSG_WAITALL);
        if (r <= 0) return false;
        total += r;
    }
    return true;
}

bool writeFrame(int fd, const std::string& json_payload) {
    std::string framed = exec_proto::frame(json_payload);
    size_t sent = 0;
    while (sent < framed.size()) {
        ssize_t r = ::send(fd, framed.data() + sent, framed.size() - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}
#endif

// Streambuf that forwards writes as framed JSON over the IPC handle.
template<typename Handle>
class IpcStreambuf : public std::streambuf {
public:
    IpcStreambuf(Handle h, const char* tag) : h_(h), tag_(tag) {
        setp(buf_, buf_ + sizeof(buf_) - 1);
    }
    ~IpcStreambuf() override { sync(); }
protected:
    int overflow(int c) override {
        if (c != EOF) {
            *pptr() = (char)c;
            pbump(1);
        }
        return sync() == 0 ? c : EOF;
    }
    int sync() override {
        if (pbase() == pptr()) return 0;
        size_t n = (size_t)(pptr() - pbase());
        json j;
        j[tag_] = std::string(pbase(), n);
        if (!writeFrame(h_, j.dump())) {
            // Pipe broken — silently drop further output.
        }
        setp(buf_, buf_ + sizeof(buf_) - 1);
        return 0;
    }
private:
    Handle h_;
    const char* tag_;
    char buf_[4096];
};

// Worker — handles one client request end to end.
#ifdef _WIN32
void handleClient(HANDLE pipe_handle) {
#else
void handleClient(int sock_fd) {
    auto pipe_handle = sock_fd;
#endif
    g_active_workers.fetch_add(1);

    // Read request frame.
    std::string req_json;
    if (!readFrame(pipe_handle, req_json)) {
#ifdef _WIN32
        DisconnectNamedPipe(pipe_handle);
        CloseHandle(pipe_handle);
#else
        close(sock_fd);
#endif
        g_active_workers.fetch_sub(1);
        return;
    }

    int exit_code = 1;
    try {
        json req = json::parse(req_json, nullptr, false);
        if (req.is_discarded() || !req.is_object()) {
            // Malformed.
            json done; done["done"] = true; done["exit"] = 64;
            writeFrame(pipe_handle, done.dump());
        } else {
            std::string op = req.value("op", "exec");
            if (op != "exec") {
                json done; done["done"] = true; done["exit"] = 64;
                writeFrame(pipe_handle, done.dump());
            } else {
                std::vector<std::string> argv;
                if (req.contains("argv") && req["argv"].is_array()) {
                    for (auto& a : req["argv"]) {
                        if (a.is_string()) argv.push_back(a.get<std::string>());
                    }
                }
                std::string cwd = req.value("cwd", std::string{});
                std::string stdin_buf = req.value("stdin", std::string{});

                if (argv.empty()) {
                    json done; done["done"] = true; done["exit"] = 64;
                    writeFrame(pipe_handle, done.dump());
                } else {
                    // Build redirected cout/cerr → IPC stream.
                    IpcStreambuf<decltype(pipe_handle)> out_buf(pipe_handle, "out");
                    IpcStreambuf<decltype(pipe_handle)> err_buf(pipe_handle, "err");

                    std::lock_guard<std::mutex> lk(g_chdir_mu);
                    std::error_code ec;
                    fs::path saved_cwd = fs::current_path(ec);
                    if (ec) ec.clear();
                    if (!cwd.empty()) {
                        fs::current_path(cwd, ec);
                        if (ec) ec.clear();
                    }

                    // Redirect cin from stdin_buf (per-request).
                    std::istringstream in_stream(stdin_buf);
                    auto* old_cin  = std::cin.rdbuf(in_stream.rdbuf());
                    auto* old_cout = std::cout.rdbuf(&out_buf);
                    auto* old_cerr = std::cerr.rdbuf(&err_buf);
                    // v1.20.0 (bugfix): RAII clear projectDbOverride per request
                    // so cross-project context leak from prior `--project X`
                    // CLI does not persist in singleton Config.
                    struct OverrideClearer {
                        ~OverrideClearer() { ::icmg::core::Config::instance().clearProjectDbOverride(); }
                    } _clear_at_end;
                    ::icmg::core::Config::instance().clearProjectDbOverride(); // pre-clear
                    try {
                        auto& reg = Registry<cli::BaseCommand>::instance();
                        auto cmd = reg.create(argv.front());
                        if (!cmd) {
                            std::cerr << "icmg: unknown command '"
                                      << argv.front() << "'\n";
                            exit_code = 127;
                        } else {
                            std::vector<std::string> rest(argv.begin() + 1, argv.end());
                            exit_code = cmd->run(rest);
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "icmg: exec_server: " << e.what() << "\n";
                        exit_code = 1;
                    } catch (...) {
                        std::cerr << "icmg: exec_server: unknown exception\n";
                        exit_code = 1;
                    }
                    out_buf.pubsync();
                    err_buf.pubsync();
                    std::cin.rdbuf(old_cin);
                    std::cout.rdbuf(old_cout);
                    std::cerr.rdbuf(old_cerr);

                    // Restore cwd.
                    if (!saved_cwd.empty()) {
                        fs::current_path(saved_cwd, ec);
                        if (ec) ec.clear();
                    }

                    json done; done["done"] = true; done["exit"] = exit_code;
                    writeFrame(pipe_handle, done.dump());
                }
            }
        }
    } catch (...) {
        json done; done["done"] = true; done["exit"] = 1;
        writeFrame(pipe_handle, done.dump());
    }

#ifdef _WIN32
    FlushFileBuffers(pipe_handle);
    DisconnectNamedPipe(pipe_handle);
    CloseHandle(pipe_handle);
#else
    close(sock_fd);
#endif
    g_active_workers.fetch_sub(1);
}

} // namespace

// ── public entry ───────────────────────────────────────────────────────────

void run(std::atomic<bool>& stop_flag) {
    std::string name = exec_proto::pipeName();

#ifdef _WIN32
    while (!stop_flag.load()) {
        HANDLE h = CreateNamedPipeA(
            name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            std::cerr << "exec_server: CreateNamedPipe failed: "
                      << GetLastError() << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        BOOL connected = ConnectNamedPipe(h, nullptr)
            ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected || stop_flag.load()) {
            CloseHandle(h);
            continue;
        }
        if (g_active_workers.load() >= kMaxWorkers) {
            // Drop oversubscribed connections politely.
            json done; done["done"] = true; done["exit"] = 1;
            writeFrame(h, done.dump());
            DisconnectNamedPipe(h);
            CloseHandle(h);
            continue;
        }
        std::thread(handleClient, h).detach();
    }
#else
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) return;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, name.c_str(), sizeof(addr.sun_path) - 1);
    unlink(name.c_str());
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(srv); return; }
    if (listen(srv, 16) < 0) { close(srv); return; }
    while (!stop_flag.load()) {
        int c = accept(srv, nullptr, nullptr);
        if (c < 0) { if (errno == EINTR) continue; break; }
        if (g_active_workers.load() >= kMaxWorkers) {
            json done; done["done"] = true; done["exit"] = 1;
            writeFrame(c, done.dump());
            close(c);
            continue;
        }
        std::thread(handleClient, c).detach();
    }
    close(srv);
    unlink(name.c_str());
#endif
}

} // namespace icmg::core::exec_server
