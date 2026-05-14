#include "rule_daemon_client.hpp"
#include "rule_daemon.hpp"
#include "../core/exec_utils.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <unistd.h>
#endif

using nlohmann::json;

namespace icmg::daemon {

// ---- send/receive ----------------------------------------------------------

std::string RuleDaemonClient::sendRaw(const std::string& json_msg) {
#ifdef _WIN32
    std::string name = RuleDaemon::pipeName();
    HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";

    DWORD written = 0;
    if (!WriteFile(h, json_msg.c_str(), (DWORD)json_msg.size(), &written, nullptr)) {
        CloseHandle(h);
        return "";
    }
    // Larger buffer (64KB) for framed hook responses; PIPE_TYPE_MESSAGE
    // delivers whole message in one ReadFile call.
    std::string out;
    out.resize(65536);
    DWORD read_bytes = 0;
    BOOL ok = ReadFile(h, out.data(), (DWORD)out.size(), &read_bytes, nullptr);
    CloseHandle(h);
    if (!ok || read_bytes == 0) return "";
    out.resize(read_bytes);
    return out;

#else
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return "";

    std::string path = RuleDaemon::pipeName();
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return "";
    }
    send(sock, json_msg.c_str(), json_msg.size(), 0);

    // Loop recv until socket closes (sender done) — handles framed responses.
    std::string out;
    char buf[8192];
    while (true) {
        ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, buf + n);
        if (out.size() > 8 * 1024 * 1024) break; // 8MB cap
    }
    close(sock);
    return out;
#endif
}

// ---- framing parser --------------------------------------------------------
//
// Framed wire: "Content-Length: N\r\n\r\n<body>"
// Legacy wire: "<body>" (raw JSON, no header)

std::string RuleDaemonClient::parseFramed(const std::string& raw) {
    static const std::string PREFIX = "Content-Length: ";
    if (raw.size() < PREFIX.size() ||
        raw.compare(0, PREFIX.size(), PREFIX) != 0) {
        return raw; // legacy unframed
    }
    auto nl = raw.find("\r\n\r\n", PREFIX.size());
    if (nl == std::string::npos) return raw; // malformed; treat as legacy
    return raw.substr(nl + 4);
}

EvalResult RuleDaemonClient::parseResponse(const std::string& raw) {
    EvalResult r;
    r.action = "ALLOW";
    if (raw.empty()) return r;
    std::string body = parseFramed(raw);
    try {
        auto j    = json::parse(body);
        r.action  = j.value("action",  std::string("ALLOW"));
        r.message = j.value("message", std::string(""));
        r.suggest = j.value("suggest", std::string(""));
        r.lines   = j.value("lines",   0);
    } catch (...) {}
    return r;
}

// ---- public API ------------------------------------------------------------

EvalResult RuleDaemonClient::check(const std::string& tool,
                                    const std::string& file, int lines) {
    json req;
    req["tool"]  = tool;
    req["file"]  = file;
    req["lines"] = lines;
    auto raw = sendRaw(req.dump());
    if (raw.empty()) { EvalResult r; r.action = "ALLOW"; return r; }  // fail-open
    return parseResponse(raw);
}

bool RuleDaemonClient::ping() {
    auto raw = sendRaw("{\"tool\":\"PING\"}");
    return !raw.empty() && parseFramed(raw).find("PONG") != std::string::npos;
}

bool RuleDaemonClient::shutdown() {
    auto raw = sendRaw("{\"tool\":\"SHUTDOWN\"}");
    return !raw.empty();
}

bool RuleDaemonClient::reload() {
    auto raw = sendRaw("{\"tool\":\"RELOAD\"}");
    return !raw.empty();
}

bool RuleDaemonClient::setStrict(bool on) {
    json req;
    req["tool"] = "SET_STRICT";
    req["on"]   = on;
    auto raw = sendRaw(req.dump());
    return !raw.empty() && parseFramed(raw).find("OK") != std::string::npos;
}

bool RuleDaemonClient::getStrict() {
    auto raw = sendRaw("{\"tool\":\"GET_STRICT\"}");
    if (raw.empty()) return false;
    try {
        auto j = json::parse(parseFramed(raw));
        return j.value("strict", false);
    } catch (...) { return false; }
}

// ---- v0.56.0: hook RPC fast-path -------------------------------------------

bool RuleDaemonClient::ensureDaemon() {
    if (ping()) return true;
    // Spawn background daemon. Best-effort; fire-and-forget.
    // Use core::safeExecShell so PATH inheritance + Windows CreateProcess
    // hidden-window semantics are handled consistently.
    std::string cmd =
#ifdef _WIN32
        "start /b icmg rule-daemon start >nul 2>nul"
#else
        "icmg rule-daemon start >/dev/null 2>&1 &"
#endif
        ;
    (void)icmg::core::safeExecShell(cmd, false, 2000);
    // Poll up to 500ms for daemon to come up.
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (ping()) return true;
    }
    return false;
}

bool RuleDaemonClient::callHook(const std::string& op,
                                 const std::string& stdin_raw,
                                 std::string* out_emit) {
    // Try once without spawn. If daemon down, spawn + retry once.
    auto attempt = [&]() -> bool {
        json req;
        req["tool"]  = op;
        req["stdin"] = stdin_raw;
        auto raw = sendRaw(req.dump());
        if (raw.empty()) return false;
        std::string body = parseFramed(raw);
        try {
            auto j = json::parse(body);
            auto action = j.value("action", std::string(""));
            if (action != "OK") return false;
            if (out_emit) {
                auto e = j.value("emit", std::string(""));
                if (!e.empty()) *out_emit = e;
            }
            return true;
        } catch (...) { return false; }
    };

    if (attempt()) return true;
    if (!ensureDaemon()) return false;
    return attempt();
}

} // namespace icmg::daemon
