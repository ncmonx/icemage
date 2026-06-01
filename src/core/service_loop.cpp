#include "service_loop.hpp"
#include "path_utils.hpp"
#include "cron_store.hpp"
#include "exec_utils.hpp"
#include "config.hpp"
#include "registry.hpp"
#include "../cli/base_command.hpp"
#include "../daemon/rule_daemon.hpp"
#include "exec_server.hpp"
#include "log_rotate.hpp"
#include "prefetch_cache.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <tlhelp32.h>
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::core {

namespace {

std::atomic<bool> g_stop{false};

void installSignalHandlers() {
#ifndef _WIN32
    std::signal(SIGTERM, [](int){ g_stop = true; });
    std::signal(SIGINT,  [](int){ g_stop = true; });
#endif
}

// Per-task schedule: name → interval-seconds + argv to forward to the
// matching BaseCommand. Tunable via service-state.json overrides later.
struct TaskSpec {
    const char* name;            // service-state.json key + log label
    int         interval_secs;
    const char* cmd_name;        // Registry<BaseCommand>::create key
    std::vector<std::string> argv;
};

static std::vector<TaskSpec> defaultTasks() {
    return {
        {"backup",         60 * 60,   "backup",         {"snapshot", "--note", "auto-hourly"}},
        {"backup-prune",   60 * 60,   "backup",         {"prune"}},
        {"maintain",       60 * 60 * 6,"maintain",      {"run"}},
        {"mirror",         60 * 15,   "mirror",         {"sync"}},
        {"sentinel",       60 * 15,   "sentinel",       {"run", "--quiet"}},
        {"shadow-upgrade", 60 * 60 * 24,"shadow-upgrade",{"check"}},
    };
}

// service-state.json: { "tasks": { "<name>": { "last_success": <ts> } } }
static json readState() {
    json j;
    try {
        std::ifstream f(serviceStatePath());
        if (f) f >> j;
    } catch (...) { j = json::object(); }
    if (!j.is_object()) j = json::object();
    if (!j.contains("tasks") || !j["tasks"].is_object()) j["tasks"] = json::object();
    return j;
}

static void writeState(const json& j) {
    try {
        std::error_code ec;
        fs::create_directories(fs::path(serviceStatePath()).parent_path(), ec);
        std::ofstream f(serviceStatePath());
        f << j.dump(2);
    } catch (...) {}
}

static int64_t lastSuccess(const json& state, const std::string& task) {
    if (!state.contains("tasks")) return 0;
    if (!state["tasks"].contains(task)) return 0;
    auto& t = state["tasks"][task];
    if (!t.contains("last_success")) return 0;
    try { return t["last_success"].get<int64_t>(); } catch (...) { return 0; }
}

static void stampSuccess(json& state, const std::string& task, int64_t now) {
    state["tasks"][task]["last_success"] = now;
}

} // namespace

std::string serviceStatePath() {
    return (fs::path(icmgGlobalDir()) / "service-state.json").string();
}

std::string servicePidPath() {
    return (fs::path(icmgGlobalDir()) / "service.pid").string();
}

void ServiceLoop::requestStop() { g_stop = true; }
bool ServiceLoop::shouldStop()  { return g_stop.load(); }

void ServiceLoop::tickOnce() {
#ifdef _WIN32
    // v1.6.1: dismiss any pending "X:/ — drive not found" popup before main
    // work. SmartScreen spawns these out-of-process; SEM cannot reach them.
    // EnumWindows pass cheap (~1ms).
    EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        char title[64] = {0};
        int n = GetWindowTextA(hwnd, title, sizeof(title) - 1);
        if (n < 2 || n > 4) return TRUE;
        char c = title[0];
        if (!(c >= 'A' && c <= 'Z') || title[1] != ':') return TRUE;
        char cls[64] = {0};
        GetClassNameA(hwnd, cls, sizeof(cls) - 1);
        if (std::strcmp(cls, "#32770") != 0) return TRUE;
        PostMessageA(hwnd, WM_CLOSE, 0, 0);
        return TRUE;
    }, 0);
#endif
    // v1.13.0: rotate logs (cheap, idempotent).
    try { log_rotate::rotateIcmgLogs(); } catch (...) {}

    auto& reg = Registry<cli::BaseCommand>::instance();
    auto state = readState();
    int64_t now = (int64_t)std::time(nullptr);
    bool any_ran = false;

    for (auto& t : defaultTasks()) {
        if (g_stop.load()) break;
        int64_t since = now - lastSuccess(state, t.name);
        if (since < t.interval_secs) continue;

        try {
            auto cmd = reg.create(t.cmd_name);
            if (!cmd) continue;
            (void)cmd->run(t.argv);
            stampSuccess(state, t.name, now);
            any_ran = true;
        } catch (...) {
            // Best-effort — try again next tick.
        }
    }
    if (any_ran) writeState(state);

    // v1.6.0 + v1.11.1: fire per-project cron_jobs IN-PROCESS.
    //
    // Previous (v1.6.0-v1.11.0): each due chore spawned `cd <proj> && icmg
    // <chore>` as detached subprocess. With 10+ jobs across N projects
    // every 15min, this leaked stale icmg.exe orphans when any chore hung
    // (DB lock, wscript stuck). Process bloat + WAL contention.
    //
    // Now: tokenize chore -> registry lookup -> cmd->run(argv) directly in
    // this process. chdir saved+restored per chore. try/catch isolates
    // chore crashes from service main loop.
    try {
        CronStore cs(Config::instance().globalDbPath());
        auto due = cs.dueJobs(now);
        for (auto& j : due) {
            if (g_stop.load()) break;
            std::error_code ec;
            if (!fs::exists(j.project_path, ec)) {
                cs.removeProject(j.project_path);
                continue;
            }

            // Tokenize chore on whitespace into argv.
            std::vector<std::string> argv;
            {
                std::string cur;
                for (char c : j.chore) {
                    if (c == ' ' || c == '\t') {
                        if (!cur.empty()) { argv.push_back(cur); cur.clear(); }
                    } else cur += c;
                }
                if (!cur.empty()) argv.push_back(cur);
            }
            if (argv.empty()) { cs.markRan(j.project_path, j.chore, now); continue; }

            std::string cmd_name = argv.front();
            std::vector<std::string> rest(argv.begin() + 1, argv.end());

            // Save + switch cwd. SetCurrentDirectory is process-global -
            // safe here because tickOnce runs serially in single thread.
            fs::path saved_cwd = fs::current_path(ec);
            if (ec) { ec.clear(); }
            fs::current_path(j.project_path, ec);
            if (ec) {
                std::cerr << "icmg service: cwd switch failed for "
                          << j.project_path << ": " << ec.message() << "\n";
                ec.clear();
                continue;
            }

            try {
                auto cmd = reg.create(cmd_name);
                if (cmd) {
                    (void)cmd->run(rest);
                } else {
                    std::cerr << "icmg service: unknown chore cmd '"
                              << cmd_name << "' for " << j.project_path << "\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "icmg service: chore '" << j.chore
                          << "' threw: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "icmg service: chore '" << j.chore
                          << "' threw unknown exception\n";
            }

            // Restore cwd unconditionally - even if chore threw.
            if (!saved_cwd.empty()) {
                fs::current_path(saved_cwd, ec);
                if (ec) ec.clear();
            }

            cs.markRan(j.project_path, j.chore, now);
        }
    } catch (...) { /* swallow - cron_jobs is best-effort */ }
}

int ServiceLoop::run() {
    installSignalHandlers();

    // v1.12.0: singleton lock — refuse second icmg-service instance for
    // this user. Prevents process bloat from duplicate service starts
    // (logon trigger + manual start + Startup-folder fallback).
#ifdef _WIN32
    {
        char user[256] = {0}; DWORD len = sizeof(user);
        GetUserNameA(user, &len);
        std::string mtx_name = std::string("Global\\icmg-service-") + user;
        HANDLE mtx = CreateMutexA(nullptr, FALSE, mtx_name.c_str());
        if (mtx == nullptr) {
            std::cerr << "icmg service: mutex create failed (err="
                      << GetLastError() << ")\n";
        } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
            std::cerr << "icmg service: another instance already running"
                      << " (mutex " << mtx_name << " held). Exiting.\n";
            CloseHandle(mtx);
            return 0;  // not an error — duplicate launches are normal
        }
        // Keep handle alive for process lifetime (mutex released on exit).
    }
#endif

    // Write PID file so `icmg service status` can identify us.
    // v1.28.1 fix: previous version silently swallowed all write failures
    // (catch(...) → status reported "running: no" even with live service).
    // Now logs concrete cause + uses explicit flush+close.
    try {
        std::error_code ec;
        std::string pidp = servicePidPath();
        fs::create_directories(fs::path(pidp).parent_path(), ec);
        if (ec) {
            std::cerr << "icmg service: pidfile dir create failed: "
                      << ec.message() << " (path: " << pidp << ")\n";
        }
        std::ofstream f(pidp, std::ios::out | std::ios::trunc);
        if (!f) {
            std::cerr << "icmg service: pidfile open failed: " << pidp << "\n";
        } else {
#ifdef _WIN32
            f << (long long)_getpid();
#else
            f << (long long)getpid();
#endif
            f.flush();
            f.close();
            if (!fs::exists(pidp)) {
                std::cerr << "icmg service: pidfile written but missing on disk: "
                          << pidp << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "icmg service: pidfile write threw: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "icmg service: pidfile write threw unknown\n";
    }

    // v1.12.0: spawn rule-daemon IPC server on dedicated thread.
    // Merges what was previously a separate `icmg-rule-daemon` process.
    // Pipe protocol identical → existing clients (hooks) unaffected.
    std::thread daemon_thread;
    try {
        daemon_thread = std::thread([]{
            try {
                daemon::RuleDaemon d(Config::instance().globalDbPath());
                d.run();
            } catch (const std::exception& e) {
                std::cerr << "icmg service: rule-daemon thread threw: "
                          << e.what() << "\n";
            } catch (...) {
                std::cerr << "icmg service: rule-daemon thread threw unknown\n";
            }
        });
    } catch (...) {
        std::cerr << "icmg service: rule-daemon thread spawn failed\n";
    }

    // v1.13.0: spawn exec_server thread — handles CLI invocations
    // via per-user named pipe. Eliminates per-CLI icmg-core spawn.
    std::thread exec_thread;
    try {
        exec_thread = std::thread([]{
            try {
                exec_server::run(g_stop);
            } catch (const std::exception& e) {
                std::cerr << "icmg service: exec_server thread threw: "
                          << e.what() << "\n";
            } catch (...) {
                std::cerr << "icmg service: exec_server thread threw unknown\n";
            }
        });
    } catch (...) {
        std::cerr << "icmg service: exec_server thread spawn failed\n";
    }

    // v1.14.0 + v1.18.0: dedicated popup-killer thread. Scans for "X:/ drive
    // not found" modal dialogs every 100ms and dismisses them via
    // PostMessage(WM_CLOSE).
    //
    // v1.18.0: broaden class match — Win11 may use TaskDialogClass instead
    // of legacy #32770. Also: title-only fallback when class doesn't match
    // (covers any unknown dialog class with matching drive-letter title).
#ifdef _WIN32
    std::thread popup_thread;
    try {
        popup_thread = std::thread([]{
            while (!g_stop.load()) {
                EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
                    if (!IsWindowVisible(hwnd)) return TRUE;
                    char title[64] = {0};
                    int n = GetWindowTextA(hwnd, title, sizeof(title) - 1);
                    if (n < 2 || n > 4) return TRUE;
                    char c = title[0];
                    if (!(c >= 'A' && c <= 'Z') || title[1] != ':') return TRUE;
                    // Title matches drive-letter pattern. Check class but
                    // accept multiple known dialog classes.
                    char cls[64] = {0};
                    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
                    bool known_dialog =
                        std::strcmp(cls, "#32770") == 0 ||           // legacy MessageBox
                        std::strcmp(cls, "TaskDialogClass") == 0 ||  // Win11 task dialogs
                        std::strncmp(cls, "Direct", 6) == 0;         // DirectUIHWND variants
                    if (!known_dialog) {
                        // Title pattern strong signal — verify it's a dialog
                        // (popup, modal-ish) via window style.
                        LONG style = GetWindowLongA(hwnd, GWL_STYLE);
                        if (!(style & WS_POPUP)) return TRUE;
                    }
                    PostMessageA(hwnd, WM_CLOSE, 0, 0);
                    return TRUE;
                }, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    } catch (...) {
        std::cerr << "icmg service: popup-killer thread spawn failed\n";
    }
#endif

    // v1.18.0: prefetch hot data into RAM cache for sub-ms first-prompt
    // serve. Runs once on warm path; ~50-200ms one-time cost.
    try {
        prefetch_cache::warm();
    } catch (...) { /* best-effort */ }

    std::cerr << "icmg service: started, tick=30s, rule-daemon + exec-server + popup-killer + prefetch embedded\n";
    while (!g_stop.load()) {
        tickOnce();
        for (int i = 0; i < 30 && !g_stop.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Detach daemon + exec threads — pipes block on accept; clean shutdown
    // via SHUTDOWN RPC would require sending to self. Detach acceptable
    // because process exits anyway; OS reclaims pipes + threads.
    if (daemon_thread.joinable()) daemon_thread.detach();
    if (exec_thread.joinable())   exec_thread.detach();
#ifdef _WIN32
    if (popup_thread.joinable())  popup_thread.detach();
#endif

    // Cleanup PID file.
    std::error_code ec;
    fs::remove(servicePidPath(), ec);
    std::cerr << "icmg service: stopped\n";
    return 0;
}

} // namespace icmg::core
