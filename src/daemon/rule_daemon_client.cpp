#include "rule_daemon_client.hpp"
#include "rule_daemon.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <stdexcept>

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
    char buf[8192] = {};
    DWORD read_bytes = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &read_bytes, nullptr);
    CloseHandle(h);
    return std::string(buf, read_bytes);

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
    char buf[8192] = {};
    ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
    close(sock);
    if (n <= 0) return "";
    return std::string(buf, n);
#endif
}

EvalResult RuleDaemonClient::parseResponse(const std::string& raw) {
    EvalResult r;
    r.action = "ALLOW";
    if (raw.empty()) return r;
    try {
        auto j    = json::parse(raw);
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
    return !raw.empty() && raw.find("PONG") != std::string::npos;
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
    return !raw.empty() && raw.find("OK") != std::string::npos;
}

bool RuleDaemonClient::getStrict() {
    auto raw = sendRaw("{\"tool\":\"GET_STRICT\"}");
    if (raw.empty()) return false;
    try {
        auto j = json::parse(raw);
        return j.value("strict", false);
    } catch (...) { return false; }
}

} // namespace icmg::daemon
