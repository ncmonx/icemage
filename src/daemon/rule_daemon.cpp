#include "rule_daemon.hpp"
#include "../core/db.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <chrono>
#include <mutex>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <unistd.h>
  #include <signal.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;
using namespace icmg;

namespace icmg::daemon {

// ---- pipe name -------------------------------------------------------------

std::string RuleDaemon::pipeName() {
#ifdef _WIN32
    return "\\\\.\\pipe\\icmg-rule-daemon";
#else
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (!home) home = "/tmp";
    return std::string(home) + "/.icmg/rule-daemon.sock";
#endif
}

// ---- constructor / destructor ----------------------------------------------

RuleDaemon::RuleDaemon(const std::string& db_path) : db_path_(db_path) {
    loadRules();
    buildDispatcher();
}

RuleDaemon::~RuleDaemon() {
#ifdef _WIN32
    if (pipe_handle_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_handle_);
#else
    if (sock_fd_ >= 0) close(sock_fd_);
#endif
}

// ---- rule loading ----------------------------------------------------------

void RuleDaemon::loadRules() {
    std::lock_guard<std::mutex> g(rules_mu_);
    rules_.clear();

    // Default built-in rules (thresholds: warn=200, block=500)
    for (auto* tool : {"Read", "Glob", "Grep"}) {
        RuleEntry e;
        e.tool            = tool;
        e.threshold_warn  = 200;
        e.threshold_block = 500;
        e.suggest_tmpl    = std::string("icmg context {file}");
        e.active          = true;
        rules_.push_back(e);
    }

    // Load custom overrides from DB `rules` table (rule_type='enforcement')
    try {
        core::Db db(db_path_);
        db.query(
            "SELECT name, content, active FROM rules WHERE rule_type='enforcement' AND active=1",
            {},
            [&](const core::Row& r) {
                if (r.size() < 2) return;
                try {
                    auto j = json::parse(r[1]);
                    RuleEntry e;
                    e.tool            = j.value("tool", std::string("Read"));
                    e.threshold_warn  = j.value("threshold_warn",  200);
                    e.threshold_block = j.value("threshold_block", 500);
                    e.suggest_tmpl    = j.value("suggest", std::string("icmg context {file}"));
                    e.strict_mode     = j.value("strict", false);
                    e.active          = (r.size() > 2 && r[2] == "1");
                    for (auto& existing : rules_) {
                        if (existing.tool == e.tool) { existing = e; return; }
                    }
                    rules_.push_back(e);
                } catch (...) {}
            }
        );
    } catch (...) {
        // DB not accessible — continue with defaults
    }
}

void RuleDaemon::reloadRules() { loadRules(); }

// ---- line counting ---------------------------------------------------------

int RuleDaemon::countLines(const std::string& path, int max_count) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return 0;
    int count = 0;
    char buf[8192];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        auto n = f.gcount();
        for (auto i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                ++count;
                if (count >= max_count) return count;
            }
        }
    }
    return count;
}

std::string RuleDaemon::resolveSuggest(const std::string& tmpl, const std::string& file) {
    std::string out = tmpl;
    auto pos = out.find("{file}");
    if (pos != std::string::npos) out.replace(pos, 6, file);
    return out;
}

// ---- evaluate --------------------------------------------------------------

RuleDaemon::CheckResult RuleDaemon::checkFile(const std::string& tool,
                                               const std::string& file,
                                               int hint_lines) const {
    CheckResult r;
    r.action = "ALLOW";

    // Snapshot the matched rule under lock (cheap copy), then release.
    RuleEntry rule_copy;
    bool found = false;
    {
        std::lock_guard<std::mutex> g(rules_mu_);
        for (auto& e : rules_) {
            if (e.active && (e.tool == tool || e.tool == "*")) {
                rule_copy = e;
                found = true;
                break;
            }
        }
    }
    if (!found) return r;
    const RuleEntry* rule = &rule_copy;

    // Strict mode: block ALL reads regardless of file size
    if (rule->strict_mode && !file.empty()) {
        r.action  = "BLOCK";
        r.suggest = resolveSuggest(rule->suggest_tmpl, file);
        r.message = "strict mode: use " + r.suggest + " (bypass: ICMG_STRICT_BYPASS=1)";
        return r;
    }

    int lines = hint_lines;
    if (lines == 0 && !file.empty()) {
        namespace fs = std::filesystem;
        if (fs::exists(file))
            lines = countLines(file, rule->threshold_block + 10);
    }
    if (lines == 0) return r;

    if (lines >= rule->threshold_block) {
        r.action  = "BLOCK";
        r.lines   = lines;
        r.suggest = resolveSuggest(rule->suggest_tmpl, file);
        r.message = "file " + std::to_string(lines) + " lines exceeds "
                  + std::to_string(rule->threshold_block) + "-line limit — use: " + r.suggest;
    } else if (lines >= rule->threshold_warn) {
        r.action  = "WARN";
        r.lines   = lines;
        r.suggest = resolveSuggest(rule->suggest_tmpl, file);
        r.message = "file " + std::to_string(lines) + " lines — consider: " + r.suggest;
    }
    return r;
}

// ---- dispatcher map (B2) ---------------------------------------------------

void RuleDaemon::buildDispatcher() {
    handlers_["SHUTDOWN"] = [](const std::string&) {
        return std::string("{\"action\":\"SHUTDOWN\"}");
    };
    handlers_["RELOAD"] = [](const std::string&) {
        return std::string("{\"action\":\"RELOADED\"}");
    };
    handlers_["PING"] = [](const std::string&) {
        return std::string("{\"action\":\"PONG\"}");
    };
    handlers_["SET_STRICT"] = [this](const std::string& body) {
        bool on = false;
        try {
            auto req = json::parse(body);
            on = req.value("on", false);
        } catch (...) {}
        {
            std::lock_guard<std::mutex> g(rules_mu_);
            for (auto& e : rules_) e.strict_mode = on;
        }
        return std::string("{\"action\":\"OK\",\"strict\":") + (on ? "true" : "false") + "}";
    };
    handlers_["GET_STRICT"] = [this](const std::string&) {
        bool on = false;
        {
            std::lock_guard<std::mutex> g(rules_mu_);
            on = !rules_.empty() && rules_[0].strict_mode;
        }
        return std::string("{\"action\":\"OK\",\"strict\":") + (on ? "true" : "false") + "}";
    };
}

std::string RuleDaemon::dispatch(const std::string& request_json) const {
    try {
        auto req = json::parse(request_json);
        std::string tool = req.value("tool", std::string(""));

        // Control / future-hook ops via dispatcher map.
        auto it = handlers_.find(tool);
        if (it != handlers_.end()) return it->second(request_json);

        // Default: file-rule evaluation (Read / Glob / Grep / *).
        std::string file = req.value("file", std::string(""));
        int hint_lines   = req.value("lines", 0);
        auto r = checkFile(tool, file, hint_lines);
        json res;
        res["action"] = r.action;
        if (!r.message.empty()) res["message"] = r.message;
        if (!r.suggest.empty()) res["suggest"] = r.suggest;
        if (r.lines > 0)        res["lines"]   = r.lines;
        return res.dump();

    } catch (...) {
        return "{\"action\":\"ALLOW\"}";
    }
}

// ---- platform server -------------------------------------------------------
// B2: POSIX path spawns detached worker thread per request so slow hook ops
// (B3+) don't starve concurrent rule-eval clients. Windows path keeps single
// pipe instance for now (multi-instance support is a B5 follow-up — each
// detached worker would need its own pipe handle from PIPE_UNLIMITED_INSTANCES).

#ifdef _WIN32

bool RuleDaemon::createPipe() {
    std::string name = pipeName();
    pipe_handle_ = CreateNamedPipeA(
        name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        4096, 4096, 0, nullptr
    );
    return pipe_handle_ != INVALID_HANDLE_VALUE;
}

void RuleDaemon::servePipe() {
    while (true) {
        if (!ConnectNamedPipe(pipe_handle_, nullptr)) {
            if (GetLastError() != ERROR_PIPE_CONNECTED) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }
        char buf[8192];
        DWORD read_bytes = 0;
        bool ok = (ReadFile(pipe_handle_, buf, sizeof(buf) - 1, &read_bytes, nullptr)
                   && read_bytes > 0);
        if (!ok) { DisconnectNamedPipe(pipe_handle_); continue; }
        buf[read_bytes] = '\0';
        std::string request(buf, read_bytes);

        std::string response = dispatch(request);
        bool is_shutdown = (response.find("SHUTDOWN") != std::string::npos);
        bool is_reload   = (response.find("RELOADED") != std::string::npos);

        DWORD written = 0;
        WriteFile(pipe_handle_, response.c_str(), (DWORD)response.size(), &written, nullptr);
        DisconnectNamedPipe(pipe_handle_);

        if (is_reload)   reloadRules();
        if (is_shutdown) break;
    }
}

int RuleDaemon::run() {
    if (!createPipe()) {
        std::cerr << "rule-daemon: failed to create pipe " << pipeName() << "\n";
        return 1;
    }
    std::cout << "rule-daemon: listening on " << pipeName() << "\n";
    servePipe();
    return 0;
}

#else

bool RuleDaemon::createSocket() {
    sock_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd_ < 0) return false;

    std::string path = pipeName();
    fs::create_directories(fs::path(path).parent_path());
    ::unlink(path.c_str());

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(sock_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) return false;
    if (listen(sock_fd_, 10) < 0) return false;
    return true;
}

void RuleDaemon::serveSocket() {
    while (true) {
        int client = accept(sock_fd_, nullptr, nullptr);
        if (client < 0) continue;

        // Hand off to detached worker so accept loop is freed immediately.
        std::thread([this, client]() {
            char buf[8192];
            ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                std::string response = dispatch(std::string(buf, n));
                send(client, response.c_str(), response.size(), 0);
                if (response.find("RELOADED") != std::string::npos) {
                    reloadRules();
                }
                if (response.find("SHUTDOWN") != std::string::npos) {
                    if (sock_fd_ >= 0) ::shutdown(sock_fd_, SHUT_RDWR);
                }
            }
            close(client);
        }).detach();
    }
}

int RuleDaemon::run() {
    if (!createSocket()) {
        std::cerr << "rule-daemon: failed to create socket " << pipeName() << "\n";
        return 1;
    }
    std::cout << "rule-daemon: listening on " << pipeName() << "\n";
    serveSocket();
    return 0;
}

#endif

} // namespace icmg::daemon
