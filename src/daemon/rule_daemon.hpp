#pragma once
// Rule Enforcement Daemon — persistent named-pipe server.
// Loads rules from DB into memory, evaluates PreToolUse events fast (~2-5ms IPC).
//
// Protocol (newline-delimited JSON):
//   Request:  {"tool":"Read","file":"/path/to/file.cpp","lines":823}
//   Response: {"action":"ALLOW"}
//              {"action":"WARN","message":"file 823 lines — use icmg context ..."}
//              {"action":"BLOCK","message":"file 823 lines exceeds 500-line limit"}
//
// Named pipe:
//   Windows: \\.\pipe\icmg-rule-daemon
//   POSIX:   ~/.icmg/rule-daemon.sock
//
// v0.55.0 (Phase B.B2): dispatcher map + mutex-protected rules + detached
// worker thread per request — enables future hook RPC ops without starving
// rule-eval clients.

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/socket.h>
  #include <sys/un.h>
#endif

namespace icmg::daemon {

struct RuleEntry {
    std::string tool;           // "Read" | "Glob" | "Grep" | "*"
    int         threshold_warn  = 200;   // lines
    int         threshold_block = 500;   // lines
    bool        strict_mode     = false; // block ALL reads regardless of size
    std::string suggest_tmpl;   // e.g. "icmg context {file}"
    bool        active          = true;
};

class RuleDaemon {
public:
    explicit RuleDaemon(const std::string& db_path);
    ~RuleDaemon();

    struct CheckResult {
        std::string action;   // "ALLOW" | "WARN" | "BLOCK"
        std::string message;
        std::string suggest;
        int         lines = 0;
    };

    // Start the daemon loop (blocking). Returns on SHUTDOWN command or signal.
    int run();

    // Reload rules from DB (called on --reload command or SIGHUP).
    void reloadRules();

    // Direct rule check without IPC (used by tests + foreground callers).
    CheckResult checkFile(const std::string& tool, const std::string& file, int hint_lines = 0) const;

    // Evaluate one request JSON, return response JSON string.
    // Public (B2) so tests + future direct callers skip IPC.
    std::string dispatch(const std::string& request_json) const;

    static std::string pipeName();

    // Public for tests.
    static int         countLines(const std::string& path, int max_count = 1000);
    static std::string resolveSuggest(const std::string& tmpl, const std::string& file);

private:
    std::string                    db_path_;
    mutable std::mutex             rules_mu_;
    mutable std::vector<RuleEntry> rules_;

    // Dispatcher map: op-name → handler (request_json → response_json string).
    using JsonHandler = std::function<std::string(const std::string&)>;
    std::unordered_map<std::string, JsonHandler> handlers_;
    void buildDispatcher();

    void loadRules();

#ifdef _WIN32
    HANDLE pipe_handle_ = INVALID_HANDLE_VALUE;
    bool   createPipe();
    void   servePipe();
#else
    int    sock_fd_ = -1;
    bool   createSocket();
    void   serveSocket();
#endif
};

} // namespace icmg::daemon
