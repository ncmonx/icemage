#include "rule_daemon.hpp"
#include "../core/recall_cache.hpp"
#include "../core/config.hpp"
#include "../tkil/runner.hpp"
#include "../core/json_safe.hpp"
#include "../core/sys_resources.hpp"
#include "../core/db.hpp"
#include "../core/hooks/runners.hpp"
#include "../core/recall_cache_persist.hpp"     // v1.78.3 Phase 4 persistEnabled
#include "../core/recall_cache_persist_db.hpp"  // v1.78.3 Phase 3 hydrate
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
    // v1.13.0: per-user pipe — avoids collision on multi-user servers
    // where two OS users both run icmg-service. Falls back to global
    // name when USERNAME unavailable (corner case).
    const char* user = std::getenv("USERNAME");
    if (user && *user) {
        return std::string("\\\\.\\pipe\\icmg-rule-daemon-") + user;
    }
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
    // v1.78.3 ram-brain persist wire: open Db + WriteQueue. Best-effort —
    // failure leaves persist_db_/persist_wq_ null and cache falls back to RAM-only.
    try {
        persist_db_ = std::make_unique<icmg::core::Db>(db_path);
        persist_wq_ = std::make_unique<icmg::core::WriteQueue>(100);
    } catch (...) {
        persist_db_.reset();
        persist_wq_.reset();
    }
    // v1.78.3 Phase 4: wire write-through sink. RecallCache::putAt fires the
    // sink on every PUT; sink splits the composite `scope\x1fkey` and enqueues
    // a non-blocking writeThrough() onto persist_wq_. Honors env opt-out via
    // persistEnabled() check inside the sink (cheap getenv per put).
    if (persist_db_ && persist_wq_) {
        rcache_.setPersistSink([this](const std::string& composite,
                                       const std::string& value,
                                       std::size_t bytes) {
            if (!icmg::core::persistEnabled()) return;
            auto sep = composite.find('\x1f');
            std::string scope = (sep == std::string::npos) ? std::string()
                                                            : composite.substr(0, sep);
            std::string key   = (sep == std::string::npos) ? composite
                                                            : composite.substr(sep + 1);
            // Snapshot pointers; capture-by-value-of-raw is safe because sink
            // is unset before dtor's persist_db_.reset().
            icmg::core::Db* dbp = persist_db_.get();
            persist_wq_->enqueue([dbp, scope, key, value, bytes]() {
                icmg::core::writeThrough(*dbp, scope, key, value, bytes);
            });
        });
    }
    loadRules();
    buildDispatcher();
}

RuleDaemon::~RuleDaemon() {
    // v1.78.3: unset sink first so any in-flight PUT doesn't enqueue against a
    // queue that is about to drain. Then drain + tear down Db.
    rcache_.setPersistSink({});
    if (persist_wq_) persist_wq_->flush();
    persist_wq_.reset();
    persist_db_.reset();
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

// ---- v1.78.3 Phase 3: lazy hydrate per-scope -------------------------------

void RuleDaemon::ensureScopeHydrated(const std::string& scope) const {
    if (!persist_db_) return;
    if (hydrated_scopes_.count(scope)) return;
    hydrated_scopes_.insert(scope);
    try {
        auto entries = icmg::core::hydrate(*persist_db_, scope, 256);
        for (auto& e : entries) {
            std::string composite = scope + std::string("\x1f") + e.key;
            rcache_.put(composite, e.value);
        }
    } catch (...) {
        // Best-effort: hydrate failure leaves scope empty; future PUT/GET still work.
    }
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

    // v0.56.0 (B3-B6): hook RPC ops. Each unwraps {"stdin":"<raw>"}, calls
    // the corresponding runner in core::hooks, returns {"action":"OK","emit":"..."}.
    handlers_["hook_stop"] = [](const std::string& body) {
        std::string stdin_raw;
        try {
            auto req = json::parse(body);
            stdin_raw = req.value("stdin", std::string(""));
        } catch (...) {}
        icmg::core::hooks::runStopHook(stdin_raw);
        return std::string("{\"action\":\"OK\"}");
    };
    handlers_["hook_precompact"] = [](const std::string& body) {
        std::string stdin_raw;
        try {
            auto req = json::parse(body);
            stdin_raw = req.value("stdin", std::string(""));
        } catch (...) {}
        auto emit = icmg::core::hooks::runPreCompactHook(stdin_raw);
        json out;
        out["action"] = "OK";
        if (!emit.empty()) out["emit"] = emit;
        return out.dump();
    };
    handlers_["hook_posttool_read"] = [](const std::string& body) {
        std::string stdin_raw;
        try {
            auto req = json::parse(body);
            stdin_raw = req.value("stdin", std::string(""));
        } catch (...) {}
        auto emit = icmg::core::hooks::runPostToolUseReadHook(stdin_raw);
        json out;
        out["action"] = "OK";
        if (!emit.empty()) out["emit"] = emit;
        return out.dump();
    };

    // ram-brain: daemon-shared hot recall cache. Payload carried in "stdin".
    // v1.78.3 scope ext: top-level "scope" field on request prefixes the cache
    // key with `scope + \x1f` so per-project entries don't collide. Empty/
    // missing scope = legacy "" bucket (back-compat with v1.77 clients).
    handlers_["RCACHE_GET"] = [this](const std::string& body) {
        try { auto j = json::parse(body); std::string k = j.value("stdin", std::string(""));
              std::string scope = j.value("scope", std::string(""));
              ensureScopeHydrated(scope);   // v1.78.3 Phase 3: lazy hydrate first-touch
              std::string composite = scope + std::string("\x1f") + k;
              auto v = rcache_.get(composite); json r;
              if (v) { r["value"] = *v; r["emit"] = *v; } else r["miss"] = true;
              return icmg::core::safeDump(r);
        } catch (...) { return std::string("{\"miss\":true}"); }
    };
    handlers_["RCACHE_PUT"] = [this](const std::string& body) {
        try { auto j = json::parse(body); auto inner = json::parse(j.value("stdin", std::string("{}")));
              // Scope can live on either the outer envelope or the inner payload.
              std::string scope = inner.value("scope", j.value("scope", std::string("")));
              std::string key   = inner.value("key", std::string(""));
              std::string val   = inner.value("value", std::string(""));
              ensureScopeHydrated(scope);   // v1.78.3 Phase 3: lazy hydrate first-touch
              std::string composite = scope + std::string("\x1f") + key;
              rcache_.put(composite, val); } catch (...) {}
        // ram-brain governor: self-checkup every 32 puts (adaptive cap + pin hot).
        if (++rcache_puts_ % 32 == 0)
            icmg::core::runGovernorOnce(rcache_, icmg::core::availableRamMB(), icmg::core::totalRamMB());
        return std::string("{\"ok\":true}");
    };
    handlers_["RCACHE_FLUSH"] = [this](const std::string&) { rcache_.flush(); return std::string("{\"ok\":true}"); };
    handlers_["RCACHE_STATS"] = [this](const std::string&) {
        auto st = rcache_.stats(); json r;
        r["hits"] = st.hits; r["misses"] = st.misses; r["entries"] = st.entries;
        r["bytes"] = st.bytes; r["evictions"] = st.evictions; r["cap_bytes"] = st.cap_bytes;
        return icmg::core::safeDump(r);
    };
}

// ---- response framing (B5) -------------------------------------------------
// Responses >= 4KB get a `Content-Length: N\r\n\r\n` prefix so the client can
// distinguish complete payloads from socket-close-terminated short ones.
// Small responses stay unframed for backward compatibility with v0.55.x clients.

static std::string frameIfLarge(const std::string& body) {
    constexpr size_t kFrameThreshold = 4096;
    if (body.size() < kFrameThreshold) return body;
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
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

        std::string wire = frameIfLarge(response);
        DWORD written = 0;
        WriteFile(pipe_handle_, wire.c_str(), (DWORD)wire.size(), &written, nullptr);
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
    stop_maint_ = false;
    maint_thread_ = std::thread([this]{ runMaintenance(); });
    servePipe();
    stop_maint_ = true;
    if (maint_thread_.joinable()) maint_thread_.join();
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
                std::string wire = frameIfLarge(response);
                send(client, wire.c_str(), wire.size(), 0);
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
    stop_maint_ = false;
    maint_thread_ = std::thread([this]{ runMaintenance(); });
    serveSocket();
    stop_maint_ = true;
    if (maint_thread_.joinable()) maint_thread_.join();
    return 0;
}

#endif

// M6: background maintenance thread — drains CronStore::dueJobs every 60s.
// Uses global.db (cross-project cron registry). Best-effort: exceptions swallowed.
void RuleDaemon::runMaintenance() {
    const std::string global_db = core::Config::instance().globalDbPath();
    while (!stop_maint_) {
        // Sleep in 1s increments so we respond to stop_maint_ quickly.
        for (int i = 0; i < 60 && !stop_maint_; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        if (stop_maint_) break;
        try {
            core::CronStore cs(global_db);
            int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            for (auto& job : cs.dueJobs(now)) {
                const char* off = std::getenv("ICMG_CRON");
                if (off && off[0] == '0') break;
                // Security: parse chore into argv (no shell). Validate no metacharacters.
                static const std::string kBadChars = ";|&$`<>()\\\n\r";
                if (job.chore.find_first_of(kBadChars) != std::string::npos) {
                    continue; // reject malformed chore
                }
                auto argv = icmg::tkil::parseArgv("icmg " + job.chore);
                if (!argv.empty()) core::safeExec(argv, true, 30000);
                cs.markRan(job.project_path, job.chore, now);
            }
        } catch (...) {}
    }
}


} // namespace icmg::daemon

// ram-brain touch
