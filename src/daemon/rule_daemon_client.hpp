#pragma once
// Thin IPC client for rule-daemon. ~2-5ms round-trip via named pipe/socket.
//
// v0.56.0 (B3-B6): adds hook RPC fast-path + auto-spawn fallback.
//   - callHook(op, stdin) sends {"tool":"<op>","stdin":"..."} to daemon.
//   - ensureDaemon() spawns `icmg rule-daemon start --background` if down.
//   - parseFramed() handles `Content-Length: N\r\n\r\n<body>` framing on
//     responses larger than 4KB (legacy unframed shape preserved <4KB).

#include <string>
#include <vector>

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

    // v0.56.0 (B3-B6): hook RPC fast-path.
    // Sends {"tool":"<op>","stdin":"<raw_stdin>"} to daemon. Returns true on
    // successful RPC. If out_emit non-null and response contains "emit"
    // field, writes its value there. Returns false on daemon down / RPC fail
    // — caller should fall back to inline runner.
    static bool callHook(const std::string& op,
                         const std::string& stdin_raw,
                         std::string* out_emit = nullptr);

    // Spawn `icmg rule-daemon start --background` if no daemon listening.
    // Returns true if a daemon is reachable after the call (was-already-up OR
    // spawned-and-now-up within ~500ms).
    static bool ensureDaemon();

    // 2026-07-10: the argv used to spawn the background daemon (Windows:
    // cmd.exe /c start /b icmg rule-daemon start). Pure + side-effect-free so
    // it's unit-testable without actually spawning a process. Public so
    // ensureDaemon()'s regression (a stray literal "nul" file being created
    // when this used to be a shell-redirect string executed by whichever
    // shell safeExecShell picked) can be guarded directly.
    static std::vector<std::string> daemonSpawnArgv();

    // Parse a possibly-framed response. If raw starts with "Content-Length: "
    // strips the header and returns just the body. Otherwise returns raw.
    // Public for test access.
    static std::string parseFramed(const std::string& raw);

private:
    static std::string sendRaw(const std::string& json_msg);
    static EvalResult  parseResponse(const std::string& raw);
};

} // namespace icmg::daemon
