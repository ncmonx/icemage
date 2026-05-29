// Phase 81: `icmg daemon` — persistent IPC server.
//
// Named Pipe (Win: \\.\pipe\icmg-daemon) / Unix socket (POSIX: ~/.icmg/daemon.sock)
// JSON-RPC over newline-delimited messages. ~5ms client round-trip vs 360ms cold-start.
//
// Protocol: {"id":N,"method":"...","params":{...}} → {"id":N,"result":"..."} | {"id":N,"error":"..."}
// Methods: ping, hook.userprompt, recall, context-node.match, SHUTDOWN
//
// Subcommands:
//   start [--foreground]              Spawn daemon (background default); write pidfile
//   stop                              Terminate via pidfile
//   status                            Show state + IPC path
//   restart                           stop + start
//   client <method> [--param k=v ...] Send one JSON-RPC call; print result (fallback to direct)

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/json_safe.hpp"   // v1.68.1 safeDump
#include "../../core/path_utils.hpp"
#include "../../core/exec_utils.hpp"
#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <signal.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <cstring>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string daemonIpcPath() {
#ifdef _WIN32
    // v1.13.0: per-user pipe — avoids collision on multi-user servers.
    const char* user = std::getenv("USERNAME");
    if (user && *user) {
        return std::string(R"(\\.\pipe\icmg-daemon-)") + user;
    }
    return R"(\\.\pipe\icmg-daemon)";
#else
    return (fs::path(core::icmgGlobalDir()) / "daemon.sock").string();
#endif
}

static fs::path daemonPidFile() {
    return fs::path(core::icmgGlobalDir()) / "daemon.pid";
}

// ── JSON-RPC dispatcher ──────────────────────────────────────────────────────

static std::string dispatchRequest(const std::string& raw) {
    try {
        auto req = json::parse(raw);
        int id = req.value("id", 0);
        std::string method = req.value("method", std::string(""));
        auto params = req.value("params", json::object());

        json resp;
        resp["id"] = id;

        if (method == "ping") {
            resp["result"] = "pong";
        } else if (method == "SHUTDOWN") {
            resp["result"] = "shutdown";
        } else if (method == "hook.userprompt") {
            std::string prompt = params.value("prompt", std::string(""));
            if (prompt.empty()) {
                resp["error"] = "missing param: prompt";
            } else {
                std::string esc;
                for (char c : prompt) {
                    if (c == '\'') esc += "'\\''";
                    else esc += c;
                }
                auto res = core::safeExecShell("echo '" + esc + "' | icmg hook userprompt 2>/dev/null",
                                               false, 30000);
                resp["result"] = (res.exit_code == 0) ? res.out : std::string("");
            }
        } else if (method == "recall") {
            std::string query = params.value("query", std::string(""));
            if (query.empty()) {
                resp["error"] = "missing param: query";
            } else {
                std::string esc;
                for (char c : query) { if (c == '"') esc += "\\\""; else esc += c; }
                auto res = core::safeExecShell("icmg recall \"" + esc + "\" 2>/dev/null", false, 10000);
                resp["result"] = res.out;
            }
        } else if (method == "context-node.match") {
            std::string query = params.value("query", std::string(""));
            if (query.empty()) {
                resp["error"] = "missing param: query";
            } else {
                std::string esc;
                for (char c : query) { if (c == '"') esc += "\\\""; else esc += c; }
                auto res = core::safeExecShell("icmg context-node match \"" + esc + "\" 2>/dev/null",
                                               false, 10000);
                resp["result"] = res.out;
            }
        } else {
            resp["error"] = "unknown method: " + method;
        }
        return icmg::core::safeDump(resp) + "\n";
    } catch (const std::exception& e) {
        return std::string("{\"id\":0,\"error\":\"") + e.what() + "\"}\n";
    }
}

// ── platform server loops ────────────────────────────────────────────────────

#ifdef _WIN32

static int runServerLoop(const std::string& pipe_name) {
    while (true) {
        HANDLE pipe = CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 65536, 65536, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        if (!ConnectNamedPipe(pipe, nullptr)) {
            if (GetLastError() != ERROR_PIPE_CONNECTED) {
                CloseHandle(pipe); continue;
            }
        }
        char buf[65536]; DWORD rb = 0;
        if (ReadFile(pipe, buf, sizeof(buf) - 1, &rb, nullptr) && rb > 0) {
            buf[rb] = '\0';
            std::string resp = dispatchRequest(std::string(buf, rb));
            bool shutdown = resp.find("\"shutdown\"") != std::string::npos;
            DWORD wr;
            WriteFile(pipe, resp.c_str(), (DWORD)resp.size(), &wr, nullptr);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            if (shutdown) break;
        } else {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }
    return 0;
}

#else

static int runServerLoop(const std::string& sock_path) {
    ::unlink(sock_path.c_str());
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { std::cerr << "icmg daemon: socket() failed\n"; return 1; }
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "icmg daemon: bind() failed\n"; ::close(fd); return 1;
    }
    ::listen(fd, 10);
    while (true) {
        int client = ::accept(fd, nullptr, nullptr);
        if (client < 0) continue;
        char buf[65536];
        ssize_t n = ::recv(client, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            std::string resp = dispatchRequest(std::string(buf, n));
            bool shutdown = resp.find("\"shutdown\"") != std::string::npos;
            ::send(client, resp.c_str(), resp.size(), 0);
            ::close(client);
            if (shutdown) break;
        } else {
            ::close(client);
        }
    }
    ::close(fd);
    ::unlink(sock_path.c_str());
    return 0;
}

#endif

// ── thin client: connect to pipe/socket, send one request ────────────────────

static std::string clientSend(const std::string& req_json) {
#ifdef _WIN32
    HANDLE pipe = CreateFileA(daemonIpcPath().c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return "";
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    DWORD wr;
    WriteFile(pipe, req_json.c_str(), (DWORD)req_json.size(), &wr, nullptr);
    char buf[65536]; DWORD rb = 0;
    bool ok = ReadFile(pipe, buf, sizeof(buf) - 1, &rb, nullptr);
    CloseHandle(pipe);
    if (!ok || rb == 0) return "";
    buf[rb] = '\0';
    return std::string(buf, rb);
#else
    std::string path = daemonIpcPath();
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return "";
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd); return "";
    }
    ::send(fd, req_json.c_str(), req_json.size(), 0);
    char buf[65536];
    ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    ::close(fd);
    if (n <= 0) return "";
    buf[n] = '\0';
    return std::string(buf, n);
#endif
}

// ── DaemonCommand ─────────────────────────────────────────────────────────────

class DaemonCommand : public BaseCommand {
public:
    std::string name()        const override { return "daemon"; }
    std::string description() const override {
        return "Persistent IPC server — Named Pipe (Win) / Unix socket (POSIX)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg daemon <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  start [--foreground]              Spawn daemon; write pidfile\n"
            "  stop                              Terminate via pidfile\n"
            "  status                            Show state + IPC path\n"
            "  restart                           stop + start\n"
            "  client <method> [--param k=v ...] Send JSON-RPC; print result\n\n"
            "Methods: ping  hook.userprompt  recall  context-node.match  SHUTDOWN\n"
            "IPC:     " << daemonIpcPath() << "\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (sub == "start")   return cmdStart(rest);
        if (sub == "stop")    return cmdStop(rest);
        if (sub == "status")  return cmdStatus(rest);
        if (sub == "restart") { cmdStop(rest); return cmdStart(rest); }
        if (sub == "client")  return cmdClient(rest);
        std::cerr << "icmg daemon: unknown subcommand '" << sub << "'\n";
        return 1;
    }

private:
    static int64_t pidAlive(int64_t pid) {
        if (pid <= 0) return 0;
#ifdef _WIN32
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
        if (!h) return 0;
        DWORD r = WaitForSingleObject(h, 0);
        CloseHandle(h);
        return r == WAIT_TIMEOUT ? pid : 0;
#else
        return ::kill((pid_t)pid, 0) == 0 ? pid : 0;
#endif
    }

    static bool ipcAlive() {
        json req;
        req["id"] = 0;
        req["method"] = "ping";
        req["params"] = json::object();
        return !clientSend(req.dump()).empty();
    }

    int cmdStart(const std::vector<std::string>& args) {
        // v1.12.0: if icmg-service is alive, it already hosts an embedded
        // rule-daemon. No need to spawn separate icmg daemon process —
        // would just bloat per-user proc count. Exit politely.
        {
            fs::path svc_pid = fs::path(core::icmgGlobalDir()) / "service.pid";
            if (fs::exists(svc_pid)) {
                std::ifstream pf(svc_pid);
                int64_t pid = 0; pf >> pid;
                if (pid > 0 && pidAlive(pid)) {
                    std::cout << "icmg daemon: skipped — icmg-service (pid="
                              << pid << ") already hosts embedded rule-daemon\n";
                    return 0;
                }
            }
        }
        if (fs::exists(daemonPidFile())) {
            std::ifstream pf(daemonPidFile());
            int64_t pid = 0; pf >> pid;
            if (pidAlive(pid)) {
                std::cout << "icmg daemon: already running (pid=" << pid << ")\n";
                return 0;
            }
            fs::remove(daemonPidFile());
        }

        bool fg = hasFlag(args, "--foreground");
        fs::create_directories(daemonPidFile().parent_path());

        if (fg) {
#ifdef _WIN32
            int64_t pid = (int64_t)GetCurrentProcessId();
#else
            int64_t pid = (int64_t)getpid();
#endif
            std::ofstream pf(daemonPidFile());
            pf << pid << "\n";
            pf.close();
            std::cout << "icmg daemon: listening on " << daemonIpcPath()
                      << " (pid=" << pid << ")\n"
                      << "  Press Ctrl+C to stop.\n";
            return runServerLoop(daemonIpcPath());
        }

        // Background: spawn self with --foreground, detached.
#ifdef _WIN32
        {
            char self_path[1024];
            GetModuleFileNameA(nullptr, self_path, sizeof(self_path));
            std::string cmd = std::string("\"") + self_path + "\" daemon start --foreground";
            std::vector<char> cl(cmd.begin(), cmd.end());
            cl.push_back('\0');
            STARTUPINFOA si{}; si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            bool ok = CreateProcessA(nullptr, cl.data(), nullptr, nullptr, FALSE,
                                     CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                                     nullptr, nullptr, &si, &pi);
            if (!ok) {
                std::cerr << "icmg daemon: spawn failed (err=" << GetLastError() << ")\n";
                return 1;
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            for (int i = 0; i < 20; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (fs::exists(daemonPidFile())) {
                    std::ifstream pf2(daemonPidFile());
                    int64_t pid2 = 0; pf2 >> pid2;
                    std::cout << "icmg daemon: started (pid=" << pid2 << ") on " << daemonIpcPath() << "\n";
                    return 0;
                }
            }
            std::cout << "icmg daemon: started (background)\n";
            return 0;
        }
#else
        {
            pid_t child = fork();
            if (child < 0) { std::cerr << "icmg daemon: fork() failed\n"; return 1; }
            if (child > 0) {
                // Parent: wait briefly for pidfile.
                for (int i = 0; i < 20; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (fs::exists(daemonPidFile())) {
                        std::ifstream pf2(daemonPidFile());
                        int64_t pid2 = 0; pf2 >> pid2;
                        std::cout << "icmg daemon: started (pid=" << pid2 << ") on " << daemonIpcPath() << "\n";
                        return 0;
                    }
                }
                std::cout << "icmg daemon: started (background)\n";
                return 0;
            }
            // Child: detach, write pidfile, run loop.
            setsid();
            int64_t cpid = (int64_t)getpid();
            std::ofstream pf(daemonPidFile());
            pf << cpid << "\n";
            pf.close();
            int devnull = ::open("/dev/null", O_RDWR);
            if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); ::close(devnull); }
            runServerLoop(daemonIpcPath());
            return 0;
        }
#endif
    }

    int cmdStop(const std::vector<std::string>&) {
        if (!fs::exists(daemonPidFile())) {
            std::cout << "icmg daemon: not running.\n";
            return 0;
        }
        std::ifstream pf(daemonPidFile());
        int64_t pid = 0; pf >> pid;
        pf.close();

        // Graceful: SHUTDOWN via IPC.
        if (pidAlive(pid)) {
            json req;
            req["id"] = 1; req["method"] = "SHUTDOWN"; req["params"] = json::object();
            clientSend(req.dump());
            for (int i = 0; i < 5; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!pidAlive(pid)) break;
            }
        }
        // Force if still alive.
        if (pidAlive(pid)) {
#ifdef _WIN32
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
            if (h) { TerminateProcess(h, 0); CloseHandle(h); }
#else
            ::kill((pid_t)pid, SIGTERM);
#endif
        }
        fs::remove(daemonPidFile());
        std::cout << "icmg daemon: stopped (pid=" << pid << ").\n";
        return 0;
    }

    int cmdStatus(const std::vector<std::string>&) {
        std::cout << "icmg daemon status\n"
                  << "  IPC:   " << daemonIpcPath() << "\n";
        if (!fs::exists(daemonPidFile())) {
            std::cout << "  state: not running\n"
                      << "  start: icmg daemon start\n";
            return 0;
        }
        std::ifstream pf(daemonPidFile());
        int64_t pid = 0; pf >> pid;
        if (!pidAlive(pid)) {
            std::cout << "  state: stale pidfile (pid=" << pid << " dead)\n";
            return 1;
        }
        std::cout << "  state: running (pid=" << pid << ")\n"
                  << "  IPC:   " << (ipcAlive() ? "responding" : "not responding") << "\n";
        return 0;
    }

    int cmdClient(const std::vector<std::string>& args) {
        if (args.empty()) {
            std::cerr << "Usage: icmg daemon client <method> [--param key=val ...]\n";
            return 1;
        }
        std::string method = args[0];
        json params = json::object();
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--param" && i + 1 < args.size()) {
                const std::string& kv = args[++i];
                auto eq = kv.find('=');
                if (eq != std::string::npos)
                    params[kv.substr(0, eq)] = kv.substr(eq + 1);
            }
        }

        json req;
        req["id"] = 1;
        req["method"] = method;
        req["params"] = params;

        std::string raw = clientSend(req.dump());

        if (raw.empty()) {
            // Transparent fallback: daemon not running → exec equivalent directly.
            if (method == "ping") { std::cout << "pong\n"; return 0; }
            if (method == "hook.userprompt") {
                std::string prompt = params.value("prompt", std::string(""));
                if (!prompt.empty()) {
                    std::string esc;
                    for (char c : prompt) { if (c=='\'') esc+="'\\''"; else esc+=c; }
                    auto res = core::safeExecShell(
                        "echo '" + esc + "' | icmg hook userprompt 2>/dev/null", false, 30000);
                    std::cout << res.out;
                    return res.exit_code;
                }
            }
            if (method == "recall") {
                std::string q = params.value("query", std::string(""));
                if (!q.empty()) {
                    std::string esc;
                    for (char c : q) { if (c=='"') esc+="\\\""; else esc+=c; }
                    auto res = core::safeExecShell("icmg recall \"" + esc + "\"", false, 10000);
                    std::cout << res.out;
                    return res.exit_code;
                }
            }
            std::cerr << "icmg daemon client: daemon not running"
                      << " (start: icmg daemon start)\n";
            return 2;
        }

        try {
            auto resp = json::parse(raw);
            if (resp.contains("result")) {
                std::string result = resp["result"].is_string()
                    ? resp["result"].get<std::string>()
                    : icmg::core::safeDump(resp["result"]);
                std::cout << result;
                if (!result.empty() && result.back() != '\n') std::cout << '\n';
                return 0;
            }
            if (resp.contains("error")) {
                std::cerr << "error: " << resp["error"] << "\n";
                return 1;
            }
        } catch (...) {
            std::cout << raw;
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("daemon", DaemonCommand);

} // namespace icmg::cli
