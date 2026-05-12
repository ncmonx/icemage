#pragma once
// Thin IPC client for rule-daemon. ~2-5ms round-trip via named pipe/socket.
#include <string>

namespace icmg::daemon {

struct EvalResult {
    std::string action;   // "ALLOW" | "WARN" | "BLOCK" | "PONG" | "ERROR"
    std::string message;  // human-readable (set for WARN/BLOCK)
    std::string suggest;  // exact icmg command to use instead
    int         lines = 0;
};

class RuleDaemonClient {
public:
    // Check one tool event against enforcement rules. Fail-open (returns ALLOW if daemon down).
    static EvalResult check(const std::string& tool, const std::string& file = "",
                            int lines = 0);

    // Ping the daemon. Returns true if responsive.
    static bool ping();

    // Send SHUTDOWN to daemon.
    static bool shutdown();

    // Send RELOAD to daemon (refresh rules from DB).
    static bool reload();
    static bool setStrict(bool on);
    static bool getStrict();

private:
    static std::string sendRaw(const std::string& json_msg);
    static EvalResult  parseResponse(const std::string& raw);
};

} // namespace icmg::daemon
