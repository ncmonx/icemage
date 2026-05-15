#include "service_loop.hpp"
#include "path_utils.hpp"
#include "registry.hpp"
#include "../cli/base_command.hpp"

#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#ifdef _WIN32
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
}

int ServiceLoop::run() {
    installSignalHandlers();

    // Write PID file so `icmg service status` can identify us.
    try {
        std::error_code ec;
        fs::create_directories(fs::path(servicePidPath()).parent_path(), ec);
        std::ofstream f(servicePidPath());
#ifdef _WIN32
        f << (long long)_getpid();
#else
        f << (long long)getpid();
#endif
    } catch (...) {}

    std::cerr << "icmg service: started, tick=30s\n";
    while (!g_stop.load()) {
        tickOnce();
        for (int i = 0; i < 30 && !g_stop.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Cleanup PID file.
    std::error_code ec;
    fs::remove(servicePidPath(), ec);
    std::cerr << "icmg service: stopped\n";
    return 0;
}

} // namespace icmg::core
